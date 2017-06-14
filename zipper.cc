#include "zipper.h"

using namespace pcmutils;

void Zipper::Init(Handle<Object> exports) {
  Isolate *isolate = exports->GetIsolate();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, New);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(String::NewFromUtf8(isolate, "Zipper"));

  NODE_SET_PROTOTYPE_METHOD(tpl, "write", Write);

  NODE_SET_GETTER(isolate, tpl, "channelBuffers", ChannelBuffersGetter);
  NODE_SET_GETTER(isolate, tpl, "channelsReady", ChannelsReadyGetter);
  NODE_SET_GETTER(isolate, tpl, "samplesPerBuffer", SamplesPerBufferGetter);
  NODE_SET_GETTER(isolate, tpl, "zipping", ZippingGetter);

  // Persistent<Function> constructor = Persistent<Function>::New(tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, "Zipper"), tpl->GetFunction());
}

void Zipper::New(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  if (!args.IsConstructCall()) {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Use the new operator")));
    return;
  }

  REQUIRE_ARGUMENTS(isolate, 3);
  REQUIRE_ARGUMENT_FUNCTION(isolate, 2, callback);

  Zipper* zip = new Zipper();
  zip->Wrap(args.This());

  zip->channels = args[0]->Int32Value();
  zip->alignment = args[1]->Int32Value();
  zip->frameAlignment = zip->alignment * zip->channels;
  zip->callback.Reset(isolate, callback);
  zip->zipping = false;

  zip->channelBuffers.Reset(isolate, Array::New(isolate, zip->channels));

  zip->channelsReady.Reset(isolate, Array::New(isolate, zip->channels));
  for (int i = 0; i < zip->channels; i++) {
    zip->channelsReady.Get(isolate)->Set(i, Boolean::New(isolate, false));
  }

  zip->buffer = (char*)malloc(ZIP_BUFFER_SAMPLES * zip->frameAlignment);

  args.GetReturnValue().Set(args.This());
}

void Zipper::ChannelBuffersGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Zipper* zip = ObjectWrap::Unwrap<Zipper>(args.This());
  args.GetReturnValue().Set(zip->channelBuffers);
}

void Zipper::ChannelsReadyGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Zipper* zip = ObjectWrap::Unwrap<Zipper>(args.This());
  args.GetReturnValue().Set(zip->channelsReady);
}

void Zipper::SamplesPerBufferGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();
  args.GetReturnValue().Set(Integer::New(isolate, ZIP_BUFFER_SAMPLES));
}

void Zipper::ZippingGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();
  Zipper* zip = ObjectWrap::Unwrap<Zipper>(args.This());
  args.GetReturnValue().Set(Boolean::New(isolate, zip->zipping));
}

void Zipper::Write(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  REQUIRE_ARGUMENTS(isolate, 2);
  OPTIONAL_ARGUMENT_FUNCTION(isolate, 2, callback);

  Zipper* zip = ObjectWrap::Unwrap<Zipper>(args.Holder());

  COND_ERR_CALL(isolate, zip->zipping, callback, "Still zipping");
  int channel = args[0]->Int32Value();
  COND_ERR_CALL(isolate, zip->channelsReady.Get(isolate)->Get(channel)->BooleanValue(), callback, "Already Ready");

  zip->channelBuffers.Get(isolate)->Set(channel, args[1]->ToObject());
  zip->channelsReady.Get(isolate)->Set(channel, Boolean::New(isolate, true));

  WriteBaton* baton = new WriteBaton(isolate, zip, callback, channel);
  BeginWrite(baton);
}

void Zipper::BeginWrite(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoWrite, (uv_after_work_cb)AfterWrite);
}

void Zipper::DoWrite(uv_work_t* req) {
  // Just a hop thru the evq
}

void Zipper::AfterWrite(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);
  WriteBaton* baton = static_cast<WriteBaton*>(req->data);
  Zipper* zip = baton->zip;

  if (!baton->callback.IsEmpty()) {
    Local<Value> argv[1] = { v8::Local<v8::Value>() };
    TRY_CATCH_CALL(isolate, zip->handle(), baton->callback, 0, argv);
  }

  if (!zip->zipping) {
    bool ready = true;
    for (int i = 0; i < zip->channels; i++) {
      if (!zip->channelsReady.Get(isolate)->Get(i)->BooleanValue()) ready = false;
    }

    if (ready) {
      ZipBaton* zipBaton = new ZipBaton(isolate, zip);
      zip->zipping = true;
      BeginZip(zipBaton);
    }
  }

  delete baton;
}

void Zipper::BeginZip(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoZip, (uv_after_work_cb)AfterZip);
}

void Zipper::DoZip(uv_work_t* req) {
  ZipBaton* baton = static_cast<ZipBaton*>(req->data);
  Zipper* zip = baton->zip;

  for (int sample = 0; sample < ZIP_BUFFER_SAMPLES; sample++) {
    for (int channel = 0; channel < zip->channels; channel++) {
      memcpy(
        zip->buffer + (sample * zip->frameAlignment) + (channel * zip->alignment),
        baton->channelData[channel] + (sample * zip->alignment),
        zip->alignment
      );
    }
  }
}

void Zipper::AfterZip(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);
  ZipBaton* baton = static_cast<ZipBaton*>(req->data);
  Zipper* zip = baton->zip;

  size_t blen = ZIP_BUFFER_SAMPLES * zip->frameAlignment;
  MaybeLocal<Object> buffer = Buffer::New(isolate, zip->buffer, blen);

  for (int i = 0; i < zip->channels; i++) {
    zip->channelsReady.Get(isolate)->Set(i, Boolean::New(isolate, false));
  }

  for (int i = 0; i < zip->channels; i++) {
    zip->channelBuffers.Get(isolate)->Set(i, Local<Value>::New(isolate, Null(isolate)));
  }

  zip->zipping = false;
  delete baton;

  if (!zip->callback.IsEmpty()) {
    Local<Value> argv[2] = { Local<Value>::New(isolate, Null(isolate)), Local<Value>::New(isolate, buffer.ToLocalChecked()) };
    TRY_CATCH_CALL(isolate, zip->handle(), zip->callback, 2, argv);
  }
}