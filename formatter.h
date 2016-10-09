#ifndef FORMATTER_H
#define FORMATTER_H

#include <cstdlib>
#include <uv.h>
#include <node.h>
#include <node_buffer.h>
#include <node_object_wrap.h>
#include "macros.h"

#define FMT_BUFFER_SAMPLES 1024

using namespace v8;
using namespace node;

namespace pcmutils {

class Formatter;

class Formatter : public ObjectWrap {
public:
  static void Init(Handle<Object> exports);

protected:
  Formatter() : ObjectWrap(), inFormat(0), outFormat(0),
      inAlignment(0), outAlignment(0), formatting(false), buffer(NULL) {
  }

  ~Formatter() {
    inFormat = 0;
    outFormat = 0;
    inAlignment = 0;
    outAlignment = 0;
    formatting = false;
    if (buffer != NULL) free(buffer);
    buffer = NULL;
  }

  struct Baton {
    uv_work_t request;
    Formatter* fmt;

    Baton(Formatter* fmt_) : fmt(fmt_) {
      fmt->Ref();
      request.data = this;
    }
    virtual ~Baton() {
      fmt->Unref();
    }
  };

  struct FormatBaton : Baton {
    Persistent<Function> callback;
    Persistent<Object> chunk;
    size_t chunkLength;
    char* chunkData;
    int totalSamples;
    int formattedSamples;

    FormatBaton(Isolate* isolate, Formatter* fmt_, Handle<Function> cb_, Handle<Object> chunk_) : Baton(fmt_),
        chunkLength(0), chunkData(NULL), totalSamples(0), formattedSamples(0) {

      callback.Reset(isolate, cb_);
      chunk.Reset(isolate, chunk_);
      chunkData = Buffer::Data(chunk.Get(isolate));
      chunkLength = Buffer::Length(chunk.Get(isolate));
    }
    virtual ~FormatBaton() {
      callback.Reset();
      chunk.Reset();
    }
  };

  static void New(const FunctionCallbackInfo<Value>& args);
  static void Format(const FunctionCallbackInfo<Value>& args);

  static void BeginFormat(Baton* baton);
  static void DoFormat(uv_work_t* req);
  static void AfterFormat(uv_work_t* req);

  int inFormat;
  int outFormat;
  int inAlignment;
  int outAlignment;
  bool formatting;
  char* buffer;
};

}

#endif
