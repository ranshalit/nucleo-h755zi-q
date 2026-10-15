/* SPDX-License-Identifier: Apache-2.0 */
#include "speech_model.h"
#include <cerrno>
#include <cstring>
#include <new>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/depthwise_conv.h"
#include "tensorflow/lite/micro/kernels/fully_connected.h"
#include "tensorflow/lite/micro/kernels/pooling.h"
#include "tensorflow/lite/micro/kernels/softmax.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_profiler_interface.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

#if DT_NODE_EXISTS(DT_NODELABEL(speech_model_flash))
#define SPEECH_MODEL_DATA __attribute__((section(".speech_model_bank2"), used))
#else
#define SPEECH_MODEL_DATA
#endif

namespace {
alignas(16) SPEECH_MODEL_DATA const unsigned char classifier_data[] = {
#include <speech_keywords.inc>
};
alignas(16) SPEECH_MODEL_DATA const unsigned char frontend_data[] = {
#include <audio_preprocessor_int8.inc>
};
constexpr size_t frontend_arena_size = 16384;
/* DS-CNN keeps two 25x20x64 int8 activations live plus kernel scratch. */
constexpr size_t classifier_arena_size = 81920;
constexpr size_t label_count = SPEECH_MODEL_LABELS;
/* Per-op cycle totals, enabled only while `speech bench` runs. */
class OpProfiler : public tflite::MicroProfilerInterface {
public:
	static constexpr int max_ops = 24;
	bool enabled = false;
	int count = 0;
	const char *tags[max_ops] = {};
	uint64_t cycles[max_ops] = {};
	uint32_t starts[max_ops] = {};
	int next = 0;
	uint32_t BeginEvent(const char *tag) override
	{
		if (!enabled || next >= max_ops) { return max_ops; }
		int i = next++;
		tags[i] = tag;
		if (count < next) { count = next; }
		starts[i] = k_cycle_get_32();
		return (uint32_t)i;
	}
	void EndEvent(uint32_t handle) override
	{
		if (handle < max_ops) { cycles[handle] += k_cycle_get_32() - starts[handle]; }
	}
};
OpProfiler op_profiler;
alignas(16) uint8_t frontend_arena[frontend_arena_size];
alignas(16) uint8_t classifier_arena[classifier_arena_size];
tflite::MicroMutableOpResolver<18> frontend_ops;
tflite::MicroMutableOpResolver<6> classifier_ops;
alignas(tflite::MicroInterpreter) uint8_t frontend_storage[sizeof(tflite::MicroInterpreter)];
alignas(tflite::MicroInterpreter) uint8_t classifier_storage[sizeof(tflite::MicroInterpreter)];
tflite::MicroInterpreter *frontend;
tflite::MicroInterpreter *classifier;
bool registered;
bool initialized;
int16_t frame[480];
size_t frame_count;
int8_t features[49][40];
size_t feature_count;

int register_ops()
{
#define ADD(resolver, name) \
	do { if (resolver.Add##name() != kTfLiteOk) { return -EINVAL; } } while (0)
#define ADD_KERNEL(resolver, name, kernel) \
	do { if (resolver.Add##name(kernel) != kTfLiteOk) { return -EINVAL; } } while (0)
	/* int8-only kernels keep unused int4/int16 CMSIS-NN variants out of flash. */
	ADD(classifier_ops, Reshape);
	ADD_KERNEL(classifier_ops, FullyConnected, tflite::Register_FULLY_CONNECTED_INT8());
	ADD_KERNEL(classifier_ops, Conv2D, tflite::Register_CONV_2D_INT8());
	ADD_KERNEL(classifier_ops, Softmax, tflite::Register_SOFTMAX_INT8());
	ADD_KERNEL(classifier_ops, DepthwiseConv2D, tflite::Register_DEPTHWISE_CONV_2D_INT8());
	ADD_KERNEL(classifier_ops, AveragePool2D, tflite::Register_AVERAGE_POOL_2D_INT8());
	ADD(frontend_ops, Reshape);
	ADD(frontend_ops, Cast);
	ADD(frontend_ops, StridedSlice);
	ADD(frontend_ops, Concatenation);
	ADD(frontend_ops, Mul);
	ADD(frontend_ops, Add);
	ADD(frontend_ops, Div);
	ADD(frontend_ops, Minimum);
	ADD(frontend_ops, Maximum);
	ADD(frontend_ops, Window);
	ADD(frontend_ops, FftAutoScale);
	ADD(frontend_ops, Rfft);
	ADD(frontend_ops, Energy);
	ADD(frontend_ops, FilterBank);
	ADD(frontend_ops, FilterBankSquareRoot);
	ADD(frontend_ops, FilterBankSpectralSubtraction);
	ADD(frontend_ops, PCAN);
	ADD(frontend_ops, FilterBankLog);
#undef ADD_KERNEL
#undef ADD
	return 0;
}

bool tensor_matches(const TfLiteTensor *tensor, TfLiteType type, size_t elements)
{
	if (!tensor || tensor->type != type || !tensor->dims || !tensor->data.raw) {
		return false;
	}
	size_t count = 1;
	for (int i = 0; i < tensor->dims->size; ++i) {
		if (tensor->dims->data[i] <= 0) { return false; }
		count *= tensor->dims->data[i];
	}
	return count == elements;
}
}

int speech_model_init(void)
{
	if (initialized) { return speech_model_reset(); }
	if (classifier) { return -EIO; }
	if (!registered) {
		int ret = register_ops();
		if (ret) { return ret; }
		registered = true;
	}
	const auto *model = tflite::GetModel(classifier_data);
	if (model->version() != TFLITE_SCHEMA_VERSION) { return -EINVAL; }
	classifier = new (classifier_storage) tflite::MicroInterpreter(
		model, classifier_ops, classifier_arena, sizeof(classifier_arena), nullptr,
		&op_profiler);
	if (classifier->AllocateTensors() != kTfLiteOk) { return -ENOMEM; }
	if (!tensor_matches(classifier->input(0), kTfLiteInt8, 1960) ||
	    !tensor_matches(classifier->output(0), kTfLiteInt8, label_count) ||
	    classifier->output(0)->params.scale <= 0) { return -EINVAL; }
	int ret = speech_model_reset();
	initialized = ret == 0;
	return ret;
}

int speech_model_reset(void)
{
	if (!classifier) { return -ENODEV; }
	/* Signal kernels own state in their op data; rebuild to clear all of it. */
	if (frontend) { frontend->~MicroInterpreter(); frontend = nullptr; }
	std::memset(frontend_arena, 0, sizeof(frontend_arena));
	const auto *model = tflite::GetModel(frontend_data);
	if (model->version() != TFLITE_SCHEMA_VERSION) { return -EINVAL; }
	frontend = new (frontend_storage) tflite::MicroInterpreter(
		model, frontend_ops, frontend_arena, sizeof(frontend_arena));
	if (frontend->AllocateTensors() != kTfLiteOk) { return -ENOMEM; }
	if (!tensor_matches(frontend->input(0), kTfLiteInt16, 480) ||
	    !tensor_matches(frontend->output(0), kTfLiteInt8, 40)) { return -EINVAL; }
	frame_count = 0;
	feature_count = 0;
	std::memset(features, 0, sizeof(features));
	return 0;
}

int speech_model_push_16k(const int16_t *samples, size_t count)
{
	if (!frontend || (!samples && count)) { return -EINVAL; }
	while (count) {
		size_t take = MIN(count, 480 - frame_count);
		std::memcpy(frame + frame_count, samples, take * sizeof(*samples));
		frame_count += take;
		samples += take;
		count -= take;
		if (frame_count != 480) { continue; }
		std::memcpy(frontend->input(0)->data.i16, frame, sizeof(frame));
		if (frontend->Invoke() != kTfLiteOk) { return -EIO; }
		if (feature_count == 49) {
			std::memmove(features[0], features[1], 48 * 40);
			--feature_count;
		}
		std::memcpy(features[feature_count++], frontend->output(0)->data.int8, 40);
		std::memmove(frame, frame + 320, 160 * sizeof(*frame));
		frame_count = 160;
	}
	return 0;
}

bool speech_model_window_ready(void) { return feature_count == 49; }

int speech_model_latest_feature(int8_t out[40])
{
	if (!out || feature_count == 0) { return -EAGAIN; }
	std::memcpy(out, features[feature_count - 1], 40);
	return 0;
}

int speech_model_invoke(uint16_t scores[label_count], uint32_t *duration_us)
{
	if (!scores || !duration_us || !classifier) { return -EINVAL; }
	if (!speech_model_window_ready()) { return -EAGAIN; }
	std::memcpy(classifier->input(0)->data.int8, features, sizeof(features));
	uint32_t start = k_cycle_get_32();
	if (classifier->Invoke() != kTfLiteOk) { return -EIO; }
	*duration_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
	const auto *output = classifier->output(0);
	for (size_t i = 0; i < label_count; ++i) {
		int score = static_cast<int>((output->data.int8[i] - output->params.zero_point) *
					    output->params.scale * 1000.0f + 0.5f);
		scores[i] = static_cast<uint16_t>(MAX(0, MIN(1000, score)));
	}
	return 0;
}

int speech_model_bench(unsigned int runs, uint32_t *min_us, uint32_t *max_us)
{
	if (!classifier || !runs || !min_us || !max_us) { return -EINVAL; }
	*min_us = UINT32_MAX;
	*max_us = 0;
	op_profiler = OpProfiler();
	op_profiler.enabled = true;
	for (unsigned int i = 0; i < runs; ++i) {
		op_profiler.next = 0;
		uint32_t start = k_cycle_get_32();
		if (classifier->Invoke() != kTfLiteOk) { op_profiler.enabled = false; return -EIO; }
		uint32_t us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
		*min_us = MIN(*min_us, us);
		*max_us = MAX(*max_us, us);
	}
	op_profiler.enabled = false;
	for (int i = 0; i < op_profiler.count; ++i) {
		printk("speech op %d %s %u us\n", i, op_profiler.tags[i] ? op_profiler.tags[i] : "?",
		       k_cyc_to_us_floor32((uint32_t)(op_profiler.cycles[i] / runs)));
	}
	return 0;
}

size_t speech_model_arena_used(bool is_frontend)
{
	auto *interpreter = is_frontend ? frontend : classifier;
	return interpreter ? interpreter->arena_used_bytes() : 0;
}
