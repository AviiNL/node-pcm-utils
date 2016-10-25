#include "unzipper.h"

using namespace pcmutils;

void Unzipper::Init(Handle<Object> exports) {
  Isolate *isolate = exports->GetIsolate();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(String::NewFromUtf8(isolate, "Unzipper"));

  NODE_SET_PROTOTYPE_METHOD(tpl, "unzip", Unzip);

  // Persistent<Function> constructor = Persistent<Function>::New(isolate, tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, "Unzipper"), tpl->GetFunction());
}

void Unzipper::New(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  if (!args.IsConstructCall()) {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Use the new operator")));
    return;
  }

  REQUIRE_ARGUMENTS(isolate, 2);

  Unzipper* unz = new Unzipper();
  unz->Wrap(args.This());

  unz->channels = args[0]->Int32Value();
  unz->alignment = args[1]->Int32Value();
  unz->frameAlignment = unz->channels * unz->alignment;
  unz->unzipping = false;

  unz->channelBuffers.Reset(isolate, Array::New(isolate, unz->channels));
  for (int i = 0; i < unz->channels; i++) {
    size_t blen = unz->alignment * UNZ_BUFFER_FRAMES;
    MaybeLocal<Object> b = Buffer::New(isolate, blen);
    unz->channelBuffers.Get(isolate)->Set(i, b.ToLocalChecked());
  }

  args.GetReturnValue().Set(args.This());
}

void Unzipper::Unzip(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  REQUIRE_ARGUMENTS(isolate, 2);
  REQUIRE_ARGUMENT_FUNCTION(isolate, 1, callback);

  Unzipper* unz = ObjectWrap::Unwrap<Unzipper>(args.Holder());

  COND_ERR_CALL(isolate, unz->unzipping, callback, "Still unzipping");

  UnzipBaton* baton = new UnzipBaton(isolate, unz, callback, args[0]->ToObject());
  unz->unzipping = true;
  BeginUnzip(baton);
}

void Unzipper::BeginUnzip(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoUnzip, (uv_after_work_cb)AfterUnzip);
}

void Unzipper::DoUnzip(uv_work_t* req) {
  UnzipBaton* baton = static_cast<UnzipBaton*>(req->data);
  Unzipper* unz = baton->unz;

  int sample = 0;
  int limitFrames = baton->unzippedFrames + UNZ_BUFFER_FRAMES;
  for (; baton->unzippedFrames < limitFrames && baton->unzippedFrames < baton->totalFrames; baton->unzippedFrames++) {
    for (int channel = 0; channel < unz->channels; channel++) {
      memcpy(
        baton->channelData[channel] + (sample * unz->alignment),
        baton->chunkData + (baton->unzippedFrames * unz->frameAlignment) + (channel * unz->alignment),
        unz->alignment
      );
    }
    sample++;
  }
}

void Unzipper::AfterUnzip(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);
  UnzipBaton* baton = static_cast<UnzipBaton*>(req->data);
  Unzipper* unz = baton->unz;

  // Copy buffers because we may clobber them soon.
  Local<Array> channelBuffersCopy = Local<Array>::New(isolate, Array::New(isolate, unz->channels));
  for (int i = 0; i < unz->channels; i++) {
    // Yes, copy is ok
    size_t blen = unz->alignment * UNZ_BUFFER_FRAMES;
    MaybeLocal<Object> b = Buffer::New(isolate, Buffer::Data(unz->channelBuffers.Get(isolate)->Get(i)->ToObject()), blen);
    channelBuffersCopy->Set(i, b.ToLocalChecked());
  }

  if (baton->unzippedFrames < baton->totalFrames) {
    Local<Value> argv[3] = { Local<Value>::New(isolate, Null(isolate)), Local<Value>::New(isolate, channelBuffersCopy), Local<Value>::New(isolate, Boolean::New(isolate, false)) };
    TRY_CATCH_CALL(isolate, unz->handle(), baton->callback, 3, argv);
    BeginUnzip(baton);
    return;
  }

  unz->unzipping = false;
  Local<Value> argv[3] = { Local<Value>::New(isolate, Null(isolate)), Local<Value>::New(isolate, channelBuffersCopy), Local<Value>::New(isolate, Boolean::New(isolate, true)) };
  TRY_CATCH_CALL(isolate, unz->handle(), baton->callback, 3, argv);
  delete baton;
}