#pragma once

#include <mediaprocessor.h>

struct transcode_input_ctx_t {

	AVCodecContext* coder;
	AVFrame* frame;

};

struct transcode_output_ctx_t {

	AVIOContext* io;
	AVFormatContext* fmt;
	AVStream* stream;
	AVCodecContext* coder;
	AVFrame* frame;
	AVPacket* packet;

};

class task_transcode : public mediapro_task {

public:
	task_transcode(mediapro_output* io_output, const AVOutputFormat* format, const AVCodec* codec);
	virtual void init(AVStream* stream) override;
	virtual void process(AVPacket* packet) override;
	virtual void close() override;
	virtual ~task_transcode();

private:
	mediapro_output* io_output;
	const AVOutputFormat* format;
	const AVCodec* codec;

	transcode_input_ctx_t input;
	transcode_output_ctx_t output;

	AVAudioFifo* fifo;
	SwrContext* swr;
	int64_t output_pts;
	uint8_t** convert_buffer;
	int max_frame_size;

	void ensure_frame_size(int new_maximum);
	void encode_fifo_frame(int frame_size);
	void push_frame(bool flush);

};