#include <stdexcept>
#include "util.h"

int ensure_success(const char* name, int result) {
	if (result < 0) {
		//Failed; Get error string
		char underlying[256];
		av_make_error_string(underlying, sizeof(underlying), result);

		//Format into new string
		char error[512];
		sprintf(error, "Error %s: %s (%i)", name, underlying, result);
		throw std::runtime_error(error);
	}
	else {
		return result;
	}
}

int mediapro_output_wrapper(void* opaque, uint8_t* buffer, int count) {
	if (((mediapro_output*)opaque)->write(buffer, count))
		return count;
	else
		return AVERROR_EOF;
}