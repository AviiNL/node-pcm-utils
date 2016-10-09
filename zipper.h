#ifndef ZIPPER_H
#define ZIPPER_H

#include <cstdlib>
#include <cstring>
#include <uv.h>
#include <node.h>
#include <node_buffer.h>
#include <node_object_wrap.h>
#include "macros.h"

#define ZIP_BUFFER_SAMPLES 1024

using namespace v8;
using namespace node;

namespace pcmutils {

class Zipper;

class Zipper : public ObjectWrap {
public:
  static void Init(Handle<Object> exports);

protected:
  Zipper() : ObjectWrap(), channels(0), alignment(0), frameAlignment(0), zipping(false), buffer(NULL) {
    channelBuffers.Reset();
    channelsReady.Reset();
    callback.Reset();
  }

  ~Zipper() {
    channels = 0;
    alignment = 0;
    frameAlignment = 0;
    zipping = false;
    if (buffer != NULL) free(buffer);
    buffer = NULL;
    channelBuffers.Reset();
    channelsReady.Reset();
    callback.Reset();
  }

  struct Baton {
    uv_work_t request;
    Zipper* zip;

    Baton(Zipper* zip_) : zip(zip_) {
      zip->Ref();
      request.data = this;
    }
    virtual ~Baton() {
      zip->Unref();
    }
  };

  struct WriteBaton : Baton {
    Persistent<Function> callback;
    int channel;

    WriteBaton(Isolate* isolate, Zipper* zip_, Handle<Function> cb_, int channel_) : Baton(zip_), channel(channel_) {
      callback.Reset(isolate, cb_);
    }
    virtual ~WriteBaton() {
      callback.Reset();
    }
  };

  struct ZipBaton : Baton {
    char** channelData;

    ZipBaton(Isolate* isolate, Zipper* zip_) : Baton(zip_), channelData(NULL) {
      channelData = (char**)malloc(zip->channels * sizeof(char*));
      for (int i = 0; i < zip->channels; i++) {
        channelData[i] = Buffer::Data(zip->channelBuffers.Get(isolate)->Get(i)->ToObject());
      }
    }
    virtual ~ZipBaton() {
      free(channelData);
    }
  };

  static void New(const FunctionCallbackInfo<Value>& args);
  static void Write(const FunctionCallbackInfo<Value>& args);
  static void ChannelBuffersGetter(Local<String>, const PropertyCallbackInfo<Value>& args);
  static void ChannelsReadyGetter(Local<String>, const PropertyCallbackInfo<Value>& args);
  static void SamplesPerBufferGetter(Local<String>, const PropertyCallbackInfo<Value>& args);
  static void ZippingGetter(Local<String>, const PropertyCallbackInfo<Value>& args);

  static void BeginWrite(Baton* baton);
  static void DoWrite(uv_work_t* req);
  static void AfterWrite(uv_work_t* req);

  static void BeginZip(Baton* baton);
  static void DoZip(uv_work_t* req);
  static void AfterZip(uv_work_t* req);

  Persistent<Array> channelBuffers;
  Persistent<Array> channelsReady;
  Persistent<Function> callback;
  int channels;
  int alignment;
  int frameAlignment;
  bool zipping;
  char* buffer;
};

}

#endif
