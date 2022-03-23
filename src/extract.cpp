#include "extract.h"

task_extract::task_extract(mediapro_output* output) {
	this->output = output;
}

void task_extract::init(AVStream* stream)
{

}

void task_extract::process(AVPacket* packet)
{
	output->write(packet->data, packet->size);
}

void task_extract::close()
{

}

task_extract::~task_extract()
{

}

/* Create function */

void mediapro_context::add_task_extract(int stream_index, mediapro_output* io_output) {
	add_task(new task_extract(io_output), stream_index);
}