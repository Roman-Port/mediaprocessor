#include <mediaprocessor.h>
#include <stdexcept>
#include <stdio.h>
#include <cassert>

mediapro_context::mediapro_context() {
	io = 0;
	fmt = 0;
	packet = 0;
	next_task = 0;
}

int mediapro_context_read(void* ctx, uint8_t* buffer, int count) {
	int read = fread(buffer, 1, count, (FILE*)ctx);
	if (read == 0)
		return AVERROR_EOF;
	else
		return read;
}

void mediapro_context::init(int buffer_size) {
	//Create AVIO context
	io = avio_alloc_context((uint8_t*)av_malloc(buffer_size), buffer_size, 0, this, &wrapper_read, NULL, NULL);
	if (io == NULL)
		throw std::runtime_error("Failed to allocate AVIO context.");

	//Allocate context
	fmt = avformat_alloc_context();
	if (fmt == NULL)
		throw std::runtime_error("Failed to allocate AVFormat context.");

	//Succes; Set the context
	fmt->pb = io;

	//Attempt to open
	if (avformat_open_input(&fmt, NULL, NULL, NULL) < 0)
		throw std::runtime_error("Failed to open AVFormat context on input.");

	//Find stream info
	if (avformat_find_stream_info(fmt, NULL) < 0)
		throw std::runtime_error("Failed to find stream info.");

	//Allocate packet
	packet = av_packet_alloc();
	if (packet == NULL)
		throw std::runtime_error("Failed to allocate input packet.");
}

void mediapro_context::add_task(mediapro_task* task, int stream_index) {
	//Allocate the task
	mediapro_context_task_t* data = (mediapro_context_task_t*)malloc(sizeof(mediapro_context_task_t));
	assert(data != 0);

	//Set up
	data->task = task;
	data->stream_index = stream_index;
	data->next = next_task;

	//Add to list
	next_task = data;
}

#define ENUMERATE_TASKS(cb) { mediapro_context_task_t* cursor = next_task; while (cursor != 0) { cb; cursor = cursor->next; }}

void mediapro_context::process() {
	//Initialize tasks
	ENUMERATE_TASKS(cursor->task->init(fmt->streams[cursor->stream_index]);)

	//Process frames
	while (av_read_frame(fmt, packet) >= 0) {
		//Find any tasks on this stream
		ENUMERATE_TASKS(if (cursor->stream_index == packet->stream_index) { cursor->task->process(packet); })

		//Free packet
		av_packet_unref(packet);
	}

	//Close tasks
	ENUMERATE_TASKS(cursor->task->close();)
}

int mediapro_context::wrapper_read(void* opaque, uint8_t* ctx, int count) {
	return ((mediapro_context*)opaque)->io_read(ctx, count);
}

mediapro_context::~mediapro_context() {
	//Dispose all tasks
	mediapro_context_task_t* next;
	while (next_task != 0) {
		//Delete the task itself
		delete (next_task->task);

		//Store the next task temporarily
		next = next_task->next;

		//Free the struct
		free(next_task);

		//Change reference
		next_task = next;
	}

	//Close AV stuff
	if (packet)
		av_packet_free(&packet);
	if (fmt) {
		avformat_free_context(fmt);
		fmt = 0;
	}
	if (io) {
		av_free(io->buffer);
		avio_context_free(&io);
	}
}