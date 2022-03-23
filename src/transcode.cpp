#include <stdexcept>
#include "transcode.h"
#include "util.h"

uint8_t** create_conversion_buffer(int channel_count, int num_samples, AVSampleFormat fmt) {
	//Allocate the "container" for the pointers
	uint8_t** result = (uint8_t**)malloc(sizeof(uint8_t*) * channel_count);
	if (result == NULL)
		throw std::runtime_error("Failed to allocate");

	//Create the buffer itself
	int ok = av_samples_alloc(
		result,
		NULL,
		channel_count,
		num_samples,
		fmt,
		0
	);
	if (ok < 0) {
		//Failed
		free(result);
		throw std::runtime_error("Failed to allocate temporary buffer.");
	}
	else {
		//Success
		return result;
	}
}

/// <summary>
/// Opens the input for the transcoder from a stream
/// </summary>
void transcode_open_input(transcode_input_ctx_t* ctx, AVStream* input_stream) {
	//Find the decoder for this
	const AVCodec* dec = avcodec_find_decoder(input_stream->codecpar->codec_id);
	if (dec == NULL)
		throw std::runtime_error("Failed to find decoder for input stream.");

	//Allocate the decoder context for this
	ctx->coder = avcodec_alloc_context3(dec);
	if (ctx->coder == NULL)
		throw std::runtime_error("Failed to allocate stream decoder.");

	//Copy parameters to decoder
	if (avcodec_parameters_to_context(ctx->coder, input_stream->codecpar) < 0)
		throw std::runtime_error("Failed to copy stream parameters to decoder.");

	//Open the decoder
	if (avcodec_open2(ctx->coder, dec, NULL) < 0)
		throw std::runtime_error("Failed to open decoder.");

	//Allocate frame
	ctx->frame = av_frame_alloc();
	if (ctx->frame == NULL)
		throw std::runtime_error("Failed to allocate decoder frame.");
}

/// <summary>
/// Opens the output for the transcoder
/// </summary>
void transcode_open_output(transcode_output_ctx_t* ctx, mediapro_output* io_out, AVCodecContext* info_source, const AVOutputFormat* format, const AVCodec* codec) {
	//Create AVIO context
	ctx->io = avio_alloc_context((uint8_t*)av_malloc(io_out->get_buffer_size()), io_out->get_buffer_size(), 1, io_out, NULL, &mediapro_output_wrapper, NULL);
	if (ctx->io == NULL)
		throw std::runtime_error("Failed to allocate AVIO context.");

	//Create context
	if (avformat_alloc_output_context2(&ctx->fmt, format, NULL, NULL) < 0)
		throw std::runtime_error("Failed to allocate output AVFormat context.");

	//Set
	ctx->fmt->pb = ctx->io;

	//Create stream
	ctx->stream = avformat_new_stream(ctx->fmt, codec);

	//Allocate encoder
	ctx->coder = avcodec_alloc_context3(codec);
	if (ctx->coder == NULL)
		throw std::runtime_error("Failed to open stream encoder.");

	//Copy parameters from the source
	if (info_source->codec_type == AVMEDIA_TYPE_VIDEO) {
		ctx->coder->height = info_source->height;
		ctx->coder->width = info_source->width;
		ctx->coder->sample_aspect_ratio = info_source->sample_aspect_ratio;
		/* take first format from list of supported formats */
		if (codec->pix_fmts)
			ctx->coder->pix_fmt = codec->pix_fmts[0];
		else
			ctx->coder->pix_fmt = info_source->pix_fmt;
		/* video time_base can be set to whatever is handy and supported by encoder */
		ctx->coder->time_base = av_inv_q(info_source->framerate);
	}
	else {
		ctx->coder->sample_rate = info_source->sample_rate;
		av_channel_layout_default(&ctx->coder->ch_layout, 2);
		/* take first format from list of supported formats */
		ctx->coder->sample_fmt = codec->sample_fmts[0];
		ctx->coder->time_base.num = 1;
		ctx->coder->time_base.den = ctx->coder->sample_rate;
	}
	if (info_source->flags & AVFMT_GLOBALHEADER)
		ctx->coder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	//Open encoder
	if (avcodec_open2(ctx->coder, codec, NULL) < 0)
		throw std::runtime_error("Failed to open encoder.");

	//Copy settings
	if (avcodec_parameters_from_context(ctx->stream->codecpar, ctx->coder) < 0)
		throw std::runtime_error("Failed to copy codec parameters.");

	//Write headers
	if (avformat_write_header(ctx->fmt, NULL) < 0)
		throw std::runtime_error("Failed to write output headers.");

	//Allocate frame
	ctx->frame = av_frame_alloc();
	if (ctx->frame == NULL)
		throw std::runtime_error("Failed to allocate encoder frame.");

	//Configure frame
	av_channel_layout_copy(&ctx->frame->ch_layout, &info_source->ch_layout);
	ctx->frame->format = ctx->coder->sample_fmt;
	ctx->frame->sample_rate = ctx->coder->sample_rate;
	ctx->frame->nb_samples = ctx->coder->frame_size;
	if (av_frame_get_buffer(ctx->frame, 0) < 0)
		throw std::runtime_error("Failed to create configured output frame buffer.");

	//Allocate packet
	ctx->packet = av_packet_alloc();
	if (ctx->packet == NULL)
		throw std::runtime_error("Failed to allocate encoder packet.");
}

task_transcode::task_transcode(mediapro_output* io_output, const AVOutputFormat* format, const AVCodec* codec)
{
	this->io_output = io_output;
	this->format = format;
	this->codec = codec;
	input.coder = 0;
	input.frame = 0;
	output.io = 0;
	output.fmt = 0;
	output.stream = 0;
	output.coder = 0;
	output.frame = 0;
	output.packet = 0;
	fifo = 0;
	swr = 0;
	output_pts = 0;
	convert_buffer = 0;
	max_frame_size = 0;
}

void task_transcode::init(AVStream* stream)
{
	//Open input
	transcode_open_input(&input, stream);

	//Open output
	transcode_open_output(&output, io_output, input.coder, format, codec);

	//Open resampler
	ensure_success("creating resampler",
		swr_alloc_set_opts2(
			&swr,
			&output.coder->ch_layout,
			output.coder->sample_fmt,
			output.coder->sample_rate,
			&input.coder->ch_layout,
			input.coder->sample_fmt,
			input.coder->sample_rate,
			0, NULL
		)
	);
	ensure_success("initializing resampler", swr_init(swr));

	//Calculate max frame size
	max_frame_size = FFMAX(input.coder->frame_size, output.coder->frame_size);

	//Create conversion buffer
	convert_buffer = create_conversion_buffer(output.coder->ch_layout.nb_channels, max_frame_size, output.coder->sample_fmt);

	//Create FIFO
	fifo = av_audio_fifo_alloc(
		output.coder->sample_fmt,
		input.coder->ch_layout.nb_channels,
		max_frame_size
	);
	if (fifo == NULL)
		throw std::runtime_error("Failed to allocate FIFO.");
}

void task_transcode::ensure_frame_size(int new_maximum)
{
	if (new_maximum > max_frame_size) {
		//Resize the FIFO
		ensure_success("resizing fifo", av_audio_fifo_realloc(fifo, new_maximum));

		//Destroy old temporary buffer
		if (convert_buffer) {
			av_freep(&convert_buffer[0]);
			free(convert_buffer);
		}

		//Allocate buffers for the resampler to hold data temporarily
		convert_buffer = create_conversion_buffer(output.coder->ch_layout.nb_channels, new_maximum, output.coder->sample_fmt);

		//Reset
		max_frame_size = new_maximum;
	}
}

void task_transcode::process(AVPacket* packet)
{
	//Unmux packet
	ensure_success("unmuxing", avcodec_send_packet(input.coder, packet));

	//Process unmuxed frames
	int ret;
	while (true) {
		//Decode frame
		ret = avcodec_receive_frame(input.coder, input.frame);
		if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
			break;
		ensure_success("decoding", ret);

		//Increase size as needed
		ensure_frame_size(input.frame->nb_samples);

		//Convert
		ensure_success("converting samples",
			swr_convert(
				swr,
				convert_buffer,
				input.frame->nb_samples,
				(const uint8_t**)input.frame->extended_data,
				input.frame->nb_samples
			)
		);

		//Write to FIFO
		if (av_audio_fifo_write(fifo, (void**)convert_buffer, input.frame->nb_samples) != input.frame->nb_samples)
			throw std::runtime_error("Failed to write to FIFO.");

		//Read full frames from the FIFO
		while (av_audio_fifo_size(fifo) >= output.coder->frame_size)
			encode_fifo_frame(output.coder->frame_size);

		//Unref frame
		av_frame_unref(input.frame);
	}
}

void task_transcode::encode_fifo_frame(int frameSize) {
	//Get frame
	AVFrame* frame = output.frame;

	//Check if reconfiguration is needed
	if (frame->nb_samples < frameSize) {
		frame->nb_samples = frameSize;
		ensure_success("creating output frame buffer", av_frame_get_buffer(frame, 0));
	}
	frame->nb_samples = frameSize;

	//Read from FIFO
	if (av_audio_fifo_read(fifo, (void**)frame->data, frameSize) < frameSize)
		throw std::runtime_error("Failed to read from FIFO.");

	//Set a timestamp based on the sample rate
	frame->pts = output_pts;
	output_pts += frame->nb_samples;

	//Push the frame
	push_frame(false);
}

void task_transcode::push_frame(bool flush) {
	//Send frame
	ensure_success("encoding (send_frame)", avcodec_send_frame(output.coder, flush ? NULL : output.frame));

	//Receive encoded frames
	int ret;
	while (true) {
		//Receive
		ret = avcodec_receive_packet(output.coder, output.packet);
		if (ret == AVERROR(EAGAIN))
			break; //More input is required
		if (ret == AVERROR_EOF)
			break; //Done
		ensure_success("encoding (receive_packet)", ret);

		//Write packet
		ensure_success("writing frame to file", av_write_frame(output.fmt, output.packet));
	}
}

void task_transcode::close()
{
	//Flush data in FIFO
	encode_fifo_frame(av_audio_fifo_size(fifo));

	//Flush
	push_frame(true);
	av_write_frame(output.fmt, NULL);

	//Write file trailer
	ensure_success("writing file trailer", av_write_trailer(output.fmt));
}

task_transcode::~task_transcode()
{
	//Clean up misc items
	if (convert_buffer) {
		av_freep(&convert_buffer[0]);
		free(convert_buffer);
	}
	if (swr)
		swr_free(&swr);
	if (fifo)
		av_audio_fifo_free(fifo);

	//Clean up input
	if (input.frame)
		av_frame_free(&input.frame);
	if (input.coder)
		avcodec_free_context(&input.coder);

	//Clean up output
	if (output.packet)
		av_packet_free(&output.packet);
	if (output.frame)
		av_frame_free(&output.frame);
	if (output.coder)
		avcodec_free_context(&output.coder);
	if (output.fmt)
		avformat_free_context(output.fmt);
	if (output.io) {
		av_free(output.io->buffer);
		avio_context_free(&output.io);
	}
}

/* Create function */

void mediapro_context::add_task_transcode(int stream_index, mediapro_output* io_output, const AVOutputFormat* format, const AVCodec* codec) {
	add_task(new task_transcode(io_output, format, codec), stream_index);
}