/* Copyright 2016 Streampunk Media Ltd.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef AUDIOINPUT_H
#define AUDIOINPUT_H

#include <nan.h>
#include <node_buffer.h>
#include <cstring>
#include <signal.h>
#include <portaudio.h>
#include <queue>
#include <string>
#include <iostream>
#include "GetDevices.h"
#include "common.h"

#define FRAMES_PER_BUFFER  (256)
using namespace std;

namespace streampunk{
  
  class AudioInput : public Nan::ObjectWrap {
  public:

    explicit AudioInput(v8::Local<v8::Array> options);
    ~AudioInput();
    
    static NAN_MODULE_INIT(Init);
    static NAN_METHOD(InputStreamStart);
    static NAN_METHOD(InputStreamStop);
    static NAN_METHOD(ReadableRead);
    static NAN_METHOD(ItemsAvailable);
    static NAN_METHOD(InputSetCallback);
    static NAN_METHOD(DisablePush);

    //Callback methods - only to be called by a callback helper
    //Do not call in user code

    int nodePortAudioInputCallback(
      const void *inputBuffer,
      void *outputBuffer,
      unsigned long framesPerBuffer,
      const PaStreamCallbackTimeInfo* timeInfo,
      PaStreamCallbackFlags statusFlags);
    void ReadableCallback(uv_async_t* req);
    
    static NAN_METHOD(New){
      if(info.IsConstructCall()){
	if (!(info.Length() == 1) && (info[0]->IsArray())){
	  return Nan::ThrowError("AudioInput constructor requires a configuration object");
	}
	v8::Local<v8::Array> params = v8::Local<v8::Array>::Cast(info[0]);
	AudioInput * obj = new AudioInput(params);
	obj->Wrap(info.This());
	info.GetReturnValue().Set(info.This());
      }else{
	const int argc = 1;
	v8::Local<v8::Value> argv[] = { info[0] };
	v8::Local<v8::Function> cons = Nan::New(constructor());
	info.GetReturnValue().Set(cons->NewInstance(argc,argv));
      }
    }

    static inline Nan::Persistent<v8::Function> & constructor() {
      static Nan::Persistent<v8::Function> my_constructor;
      return my_constructor;
    }

    Nan::Persistent<v8::Function> pushCallback;
    int enablePush = false;
    PaStream* stream;
    queue<string> bufferStack;
    uv_mutex_t padlock;

      
  private:

    int paInputInitialized = false;
    int portAudioInputStreamInitialized = false;
    uv_async_t *req;
    int sampleFormat;
    int channelCount;

  };

}//namespace streampunk
 
#endif
