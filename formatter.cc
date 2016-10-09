#include "formatter.h"

using namespace pcmutils;

void Formatter::Init(Handle<Object> exports) {
  Isolate *isolate = exports->GetIsolate();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(String::NewFromUtf8(isolate, "Formatter"));

  NODE_SET_PROTOTYPE_METHOD(tpl, "format", Format);

  // Persistent<Function> constructor = Persistent<Function>::New(isolate, tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, "Formatter"), tpl->GetFunction());
}

void Formatter::New(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  if (!args.IsConstructCall()) {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Use the new operator")));
    return;
  }

  REQUIRE_ARGUMENTS(isolate, 2);

  Formatter* fmt = new Formatter();
  fmt->Wrap(args.This());

  fmt->inFormat = args[0]->Int32Value();
  fmt->outFormat = args[1]->Int32Value();
  fmt->formatting = false;

  // TODO: lol
  if (fmt->inFormat == 0) fmt->inAlignment = 4;
  if (fmt->inFormat == 2) fmt->inAlignment = 2;
  if (fmt->inFormat == 4) fmt->inAlignment = 2;
  if (fmt->outFormat == 0) fmt->outAlignment = 4;
  if (fmt->outFormat == 2) fmt->outAlignment = 2;
  if (fmt->outFormat == 4) fmt->outAlignment = 2;

  fmt->buffer = (char*)malloc(fmt->outAlignment * FMT_BUFFER_SAMPLES);

  args.GetReturnValue().Set(args.This());
}

void Formatter::Format(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  REQUIRE_ARGUMENTS(isolate, 2);
  REQUIRE_ARGUMENT_FUNCTION(isolate, 1, callback);

  Formatter* fmt = ObjectWrap::Unwrap<Formatter>(args.Holder());

  COND_ERR_CALL(isolate, fmt->formatting, callback, "Still formatting");

  FormatBaton* baton = new FormatBaton(isolate, fmt, callback, args[0]->ToObject());
  fmt->formatting = true;
  BeginFormat(baton);
}

void Formatter::BeginFormat(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoFormat, (uv_after_work_cb)AfterFormat);
}

void Formatter::DoFormat(uv_work_t* req) {
  FormatBaton* baton = static_cast<FormatBaton*>(req->data);
  Formatter* fmt = baton->fmt;

  int chunkSamples = baton->chunkLength / fmt->inAlignment;
  int limitSamples;
  int chunkSamplesLeft = chunkSamples - baton->totalSamples;
  if (FMT_BUFFER_SAMPLES < chunkSamplesLeft) {
    limitSamples = FMT_BUFFER_SAMPLES;
  } else {
    limitSamples = chunkSamplesLeft;
  }

  if (fmt->inFormat == 0) { // F32LE

    if (fmt->outFormat == 2) {            // F32LE to S16LE

      for (int sample = 0; sample < limitSamples; sample++) {
        float* floatChunk = static_cast<float*>(static_cast<void*>(baton->chunkData));
        float floatVal = floatChunk[baton->totalSamples + sample];
        int16_t* intBuffer = static_cast<int16_t*>(static_cast<void*>(fmt->buffer));
        intBuffer[sample] = static_cast<int16_t>(floatVal * 32767);
      }

    } else if (fmt->outFormat == 4) {     // F32LE to U16LE

      for (int sample = 0; sample < limitSamples; sample++) {
        float* floatChunk = static_cast<float*>(static_cast<void*>(baton->chunkData));
        float floatVal = floatChunk[baton->totalSamples + sample];
        uint16_t* uintBuffer = static_cast<uint16_t*>(static_cast<void*>(fmt->buffer));
        uintBuffer[sample] = static_cast<uint16_t>((floatVal * 32767) + 32768);
      }

    } else {
      fprintf(stderr, "Unsupported conversion\n");
    }

  } else if (fmt->inFormat == 2) { // S16LE

    if (fmt->outFormat == 0) {            // S16LE to F32LE

      for (int sample = 0; sample < limitSamples; sample++) {
        int16_t* intChunk = static_cast<int16_t*>(static_cast<void*>(baton->chunkData));
        int16_t intVal = intChunk[baton->totalSamples + sample];
        float* floatBuffer = static_cast<float*>(static_cast<void*>(fmt->buffer));
        floatBuffer[sample] = static_cast<float>(intVal / 32768);
      }

    } else if (fmt->outFormat == 4) {     // S16LE to U16LE

      for (int sample = 0; sample < limitSamples; sample++) {
        int16_t* intChunk = static_cast<int16_t*>(static_cast<void*>(baton->chunkData));
        int16_t intVal = intChunk[baton->totalSamples + sample];
        uint16_t* uintBuffer = static_cast<uint16_t*>(static_cast<void*>(fmt->buffer));
        uintBuffer[sample] = static_cast<uint16_t>(intVal + 32768);
      }

    } else {
      fprintf(stderr, "Unsupported conversion\n");
    }

  } else if (fmt->inFormat == 4) { // U16LE

    if (fmt->outFormat == 0) {            // U16LE to F32LE

      for (int sample = 0; sample < limitSamples; sample++) {
        uint16_t* intChunk = static_cast<uint16_t*>(static_cast<void*>(baton->chunkData));
        uint16_t intVal = intChunk[baton->totalSamples + sample];
        float* floatBuffer = static_cast<float*>(static_cast<void*>(fmt->buffer));
        floatBuffer[sample] = static_cast<float>((intVal - 32768) / 32768);
      }

    } else if (fmt->outFormat == 2) {     // U16LE to S16LE

      for (int sample = 0; sample < limitSamples; sample++) {
        uint16_t* intChunk = static_cast<uint16_t*>(static_cast<void*>(baton->chunkData));
        uint16_t intVal = intChunk[baton->totalSamples + sample];
        int16_t* intBuffer = static_cast<int16_t*>(static_cast<void*>(fmt->buffer));
        intBuffer[sample] = static_cast<int16_t>(intVal - 32768);
      }

    } else {
      fprintf(stderr, "Unsupported conversion\n");
    }

  } else {
    fprintf(stderr, "Unsupported conversion\n");
  }

  baton->totalSamples += limitSamples;
  baton->formattedSamples = limitSamples;
}

void Formatter::AfterFormat(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  FormatBaton* baton = static_cast<FormatBaton*>(req->data);
  Formatter* fmt = baton->fmt;

  // Copy buffer because we may clobber them soon.
  MaybeLocal<Object> buffer = Buffer::New(isolate, fmt->buffer, baton->formattedSamples * fmt->outAlignment);

  if (baton->chunkLength / fmt->inAlignment > static_cast<size_t>(baton->totalSamples)) {
    Local<Value> argv[3] = { Local<Value>::New(isolate, Null(isolate)), Local<Value>::New(isolate, buffer.ToLocalChecked()), Local<Value>::New(isolate, Boolean::New(isolate, false)) };
    TRY_CATCH_CALL(isolate, fmt->handle(), baton->callback, 3, argv);
    BeginFormat(baton);
    return;
  }

  fmt->formatting = false;
  Local<Value> argv[3] = { Local<Value>::New(isolate, Null(isolate)), Local<Value>::New(isolate, buffer.ToLocalChecked()), Local<Value>::New(isolate, Boolean::New(isolate, true)) };
  TRY_CATCH_CALL(isolate, fmt->handle(), baton->callback, 3, argv);
  delete baton;
}