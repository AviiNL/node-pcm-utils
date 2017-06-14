#include "mixer.h"

using namespace pcmutils;

void Mixer::Init(Handle<Object> exports) {
  v8::Isolate* isolate;
  isolate = exports->GetIsolate();
  Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, Mixer::New);
  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(String::NewFromUtf8(isolate, "Mixer"));

  NODE_SET_PROTOTYPE_METHOD(tpl, "write", Write);

  NODE_SET_GETTER(isolate, tpl, "channelBuffers", ChannelBuffersGetter);
  NODE_SET_GETTER(isolate, tpl, "channelsReady", ChannelsReadyGetter);
  NODE_SET_GETTER(isolate, tpl, "samplesPerBuffer", SamplesPerBufferGetter);
  NODE_SET_GETTER(isolate, tpl, "mixing", MixingGetter);

  // Persistent<Function> constructor = Persistent<Function>::New(isolate, tpl->GetFunction());
  exports->Set(String::NewFromUtf8(isolate, "Mixer"), tpl->GetFunction());
}

void Mixer::New(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  if (!args.IsConstructCall()) {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Use the new operator")));
    return;
  }

  REQUIRE_ARGUMENTS(isolate, 4);
  REQUIRE_ARGUMENT_FUNCTION(isolate, 3, callback);

  Mixer* mix = new Mixer();
  mix->Wrap(args.This());

  mix->channels = args[0]->Int32Value();
  mix->alignment = args[1]->Int32Value();
  mix->format = args[2]->Int32Value();

  if (mix->format % 2 > 0) {
    isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, "Big-Endian formats currently unsupported by Mixer")));
    return;
  }

  mix->callback.Reset(isolate, callback);
  mix->mixing = false;

  mix->channelBuffers.Reset(isolate, Array::New(isolate, mix->channels));

  mix->channelsReady.Reset(isolate, Array::New(isolate, mix->channels));
  for (int i = 0; i < mix->channels; i++) {
    mix->channelsReady.Get(isolate)->Set(i, Boolean::New(isolate, false));
  }

  args.GetReturnValue().Set(args.This());
}

void Mixer::ChannelBuffersGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Mixer* mix = ObjectWrap::Unwrap<Mixer>(args.This());
  args.GetReturnValue().Set(mix->channelBuffers);
}

void Mixer::ChannelsReadyGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Mixer* mix = ObjectWrap::Unwrap<Mixer>(args.This());
  args.GetReturnValue().Set(mix->channelsReady);
}

void Mixer::SamplesPerBufferGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();
  args.GetReturnValue().Set(Integer::New(isolate, MIX_BUFFER_SAMPLES));
}

void Mixer::MixingGetter(Local<String>, const PropertyCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();
  Mixer* mix = ObjectWrap::Unwrap<Mixer>(args.This());
  args.GetReturnValue().Set(Boolean::New(isolate, mix->mixing));
}

void Mixer::Write(const FunctionCallbackInfo<Value>& args) {
  Isolate *isolate = args.GetIsolate();

  REQUIRE_ARGUMENTS(isolate, 2);
  OPTIONAL_ARGUMENT_FUNCTION(isolate, 2, callback);

  Mixer* mix = ObjectWrap::Unwrap<Mixer>(args.Holder());

  COND_ERR_CALL(isolate, mix->mixing, callback, "Still mixing");
  int channel = args[0]->Int32Value();
  COND_ERR_CALL(isolate, mix->channelsReady.Get(isolate)->Get(channel)->BooleanValue(), callback, "Already Ready");

  mix->channelBuffers.Get(isolate)->Set(channel, args[1]->ToObject());
  mix->channelsReady.Get(isolate)->Set(channel, Boolean::New(isolate, true));

  WriteBaton* baton = new WriteBaton(isolate, mix, callback, channel);
  BeginWrite(baton);

  args.GetReturnValue().Set(args.Holder());
}

void Mixer::BeginWrite(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoWrite, (uv_after_work_cb)AfterWrite);
}

void Mixer::DoWrite(uv_work_t* req) {
  // Just a hop thru the evq
}

void Mixer::AfterWrite(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);
  WriteBaton* baton = static_cast<WriteBaton*>(req->data);
  Mixer* mix = baton->mix;

  if (!baton->callback.IsEmpty() && baton->callback.Get(isolate)->IsFunction()) {
    Local<Value> argv[1] = { 0 };
    TRY_CATCH_CALL(isolate, mix->handle(), baton->callback, 0, argv);
  }

  if (!mix->mixing) {
    bool ready = true;
    for (int i = 0; i < mix->channels; i++) {
      if (!mix->channelsReady.Get(isolate)->Get(i)->BooleanValue()) ready = false;
    }

    if (ready) {
      MixBaton* mixBaton = new MixBaton(isolate, mix);
      mix->mixing = true;
      BeginMix(mixBaton);
    }
  }

  delete baton;
}

void Mixer::BeginMix(Baton* baton) {
  uv_queue_work(uv_default_loop(), &baton->request, DoMix, (uv_after_work_cb)AfterMix);
}

void Mixer::DoMix(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  MixBaton* baton = static_cast<MixBaton*>(req->data);
  Mixer* mix = baton->mix;

  // Save back into first channel buffer
  if (mix->format == 0) {
    float* out = static_cast<float*>(static_cast<void*>(baton->channelData[0]));
    float sum;
    for (int i = 0; i < MIX_BUFFER_SAMPLES; i++) {
      sum = 0;
      for (int c = 0; c < mix->channels; c++) {
        float* in = static_cast<float*>(static_cast<void*>(baton->channelData[c]));
        sum += in[i] / mix->channels;
      }
      out[i] = sum;
    }
  } else if (mix->format == 2) {
    int16_t* out = static_cast<int16_t*>(static_cast<void*>(baton->channelData[0]));
    int16_t sum;
    for (int i = 0; i < MIX_BUFFER_SAMPLES; i++) {
      sum = 0;
      for (int c = 0; c < mix->channels; c++) {
        int16_t* in = static_cast<int16_t*>(static_cast<void*>(baton->channelData[c]));
        sum += in[i] / mix->channels;
      }
      out[i] = sum;
    }
  } else if (mix->format == 4) {
    uint16_t* out = static_cast<uint16_t*>(static_cast<void*>(baton->channelData[0]));
    uint16_t sum;
    for (int i = 0; i < MIX_BUFFER_SAMPLES; i++) {
      sum = 0;
      for (int c = 0; c < mix->channels; c++) {
        uint16_t* in = static_cast<uint16_t*>(static_cast<void*>(baton->channelData[c]));
        sum += (in[i] - 32768) / mix->channels;
      }
      out[i] = sum + 32768;
    }
  } else {
    fprintf(stderr, "Unsupported format\n");
  }
}

void Mixer::AfterMix(uv_work_t* req) {
  Isolate *isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);
  MixBaton* baton = static_cast<MixBaton*>(req->data);
  Mixer* mix = baton->mix;

  size_t blen = Buffer::Length(mix->channelBuffers.Get(isolate)->Get(0)->ToObject());
  MaybeLocal<Object> buffer = Buffer::New(isolate, Buffer::Data(mix->channelBuffers.Get(isolate)->Get(0)->ToObject()), blen);

  for (int i = 0; i < mix->channels; i++) {
    mix->channelsReady.Get(isolate)->Set(i, Boolean::New(isolate, false));
  }

  for (int i = 0; i < mix->channels; i++) {
    mix->channelBuffers.Get(isolate)->Set(i, Local<Value>::New(isolate, Null(isolate)));
  }

  mix->mixing = false;
  delete baton;

  if (!mix->callback.IsEmpty()) {
    Local<Value> argv[2] = { Local<Value>::New(isolate, Null(isolate)), Local<Value>::New(isolate, buffer.ToLocalChecked()) };
    TRY_CATCH_CALL(isolate, mix->handle(), mix->callback, 2, argv);
  }
}