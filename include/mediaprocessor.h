#pragma once

#include <stdint.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

class mediapro_task {

public:
	virtual void init(AVStream* stream) = 0;
	virtual void process(AVPacket* packet) = 0;
	virtual void close() = 0;
	virtual ~mediapro_task() {};

};

class mediapro_output {

public:
	virtual bool write(uint8_t* buffer, int count) = 0;
	virtual int get_buffer_size() = 0;

};

struct mediapro_context_task_t {
	mediapro_task* task;
	int stream_index;
	mediapro_context_task_t* next;
};

class mediapro_context {

public:
	mediapro_context();
	void init(int buffer_size);
	void add_task_transcode(int stream_index, mediapro_output* io_output, const AVOutputFormat* format, const AVCodec* codec);
	void add_task_extract(int stream_index, mediapro_output* io_output);
	void process();
	~mediapro_context();

	AVFormatContext* fmt;

protected:
	virtual int io_read(uint8_t* ctx, int count) = 0;

private:
	void add_task(mediapro_task* task, int stream_index);
	static int wrapper_read(void* opaque, uint8_t* ctx, int count);

	AVIOContext* io;
	AVPacket* packet;

	mediapro_context_task_t* next_task;

};