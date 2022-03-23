#include <mediaprocessor.h>

class task_extract : public mediapro_task {

public:
	task_extract(mediapro_output* output);
	virtual void init(AVStream* stream) override;
	virtual void process(AVPacket* packet) override;
	virtual void close() override;
	virtual ~task_extract() override;

private:
	mediapro_output* output;

};