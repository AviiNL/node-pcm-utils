#ifndef MACROS_H
#define MACROS_H

#define NODE_SET_GETTER(isolate, target, name, function)                       \
  (target)->InstanceTemplate()                                                 \
    ->SetAccessor(String::NewFromUtf8(isolate, name), (function));

#define REQUIRE_ARGUMENTS(isolate, n)                                          \
  if (args.Length() < (n)) {                                                   \
    isolate->ThrowException(                                                   \
      Exception::TypeError(String::NewFromUtf8(isolate, "Expected " #n "arguments"))            \
    );                                                                         \
    return;                                                                    \
  }

#define COND_ERR_CALL_VOID(isolate, condition, callback, message, context)     \
  if (condition) {                                                             \
    if ((callback).IsEmpty() || !(callback)->IsFunction()) {                   \
      isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, message)));              \
      return;                                                                  \
    }                                                                          \
    Local<Value> exception = Exception::Error(String::NewFromUtf8(isolate, message));           \
    Local<Value> argv[1] = { Local<Value>::New(exception) };                   \
    TRY_CATCH_CALL(isolate, (context), (callback), 1, argv);                   \
    return;                                                                    \
  }

#define COND_ERR_CALL(isolate, condition, callback, message)                   \
  if (condition) {                                                             \
    if ((callback).IsEmpty() || !(callback)->IsFunction())                     \
      isolate->ThrowException(Exception::TypeError(String::NewFromUtf8(isolate, message)));       \
      return;                                                                  \
    Local<Value> exception = Exception::Error(String::NewFromUtf8(isolate, message));           \
    Local<Value> argv[1] = { Local<Value>::New(isolate, exception) };          \
    TRY_CATCH_CALL(isolate, args.Holder(), (callback), 1, argv);               \
    return;                                                                    \
  }

#define OPTIONAL_ARGUMENT_FUNCTION(isolate, i, var)                            \
  Local<Function> var;                                                         \
  if (args.Length() > i && !args[i]->IsUndefined()) {                          \
    if (!args[i]->IsFunction()) {                                              \
      isolate->ThrowException(Exception::TypeError(                            \
        String::NewFromUtf8(isolate, "Argument " #i " must be a function"))    \
      );                                                                       \
      return;                                                                  \
    }                                                                          \
    var = Local<Function>::Cast(args[i]);                                      \
  }

#define REQUIRE_ARGUMENT_FUNCTION(isolate, i, var)                             \
  if (args.Length() <= (i) || !args[i]->IsFunction()) {                        \
    isolate->ThrowException(Exception::TypeError(                              \
      String::NewFromUtf8(isolate, "Argument " #i " must be a function"))      \
    );                                                                         \
    return;                                                                    \
  }                                                                            \
  Local<Function> var = Local<Function>::Cast(args[i]);

#define TRY_CATCH_CALL(isolate, context, callback, argc, argv)                 \
{   TryCatch try_catch;                                                        \
    Local<Function>::New(isolate, callback)->Call(isolate->GetCurrentContext(), (context), (argc), (argv));              \
    if (try_catch.HasCaught()) {                                               \
        FatalException(isolate, try_catch);                                    \
    }                                                                          }

#endif