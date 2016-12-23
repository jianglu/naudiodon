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

#include "AudioInput.h"

using namespace std;

namespace streampunk{

#define EMIT_BUFFER_OVERRUN						\
  v8::Local<v8::Value> emitArgs[1];					\
  emitArgs[0] = Nan::New("overrun").ToLocalChecked();			\
  Nan::Call(Nan::Get(info.This(), Nan::New("emit").ToLocalChecked()).ToLocalChecked().As<v8::Function>(), \
	    info.This(), 1, emitArgs);

  int PortAudioCallbackHelper(
      const void *inputBuffer,
      void *outputBuffer,
      unsigned long framesPerBuffer,
      const PaStreamCallbackTimeInfo* timeInfo,
      PaStreamCallbackFlags statusFlags,
      void *userData)
  {  
    AudioInput *client = (AudioInput *) userData;
    return client->nodePortAudioInputCallback(
       inputBuffer,
       outputBuffer,
       framesPerBuffer,
       timeInfo,
       statusFlags
    );
  }

  void ReadableCallbackHelper(uv_async_t* req){
    AudioInput *client = (AudioInput *) req->data;
    client->ReadableCallback(req);
  }
  
  AudioInput::AudioInput(v8::Local<v8::Array> options){
    PaError err;
    PaStreamParameters inputParameters;
    Nan::MaybeLocal<v8::Object> v8Buffer;
    int sampleRate;
    char str[1000];
    Nan::MaybeLocal<v8::Value> v8Val;

    //Set up threading
    req = new uv_async_t;
    req->data = (void *)this;
    uv_async_init(uv_default_loop(),req,ReadableCallbackHelper);
    uv_mutex_init(&padlock);

    //Initialise port audio
    err = EnsureInitialized();
    if(err != paNoError) {
      sprintf(str, "Could not initialize PortAudio: %s", Pa_GetErrorText(err));
      Nan::ThrowError(str);
      return;
    }

    if(!portAudioInputStreamInitialized) {
      v8::Local<v8::FunctionTemplate> t = Nan::New<v8::FunctionTemplate>();
      t->InstanceTemplate()->SetInternalFieldCount(1);
      t->SetClassName(Nan::New("PortAudioStream").ToLocalChecked());
      portAudioInputStreamInitialized = true;
    }

    memset(&inputParameters, 0, sizeof(PaStreamParameters));

    v8Val = Nan::Get(options, Nan::New("device").ToLocalChecked());
    int deviceId = (v8Val.ToLocalChecked()->IsUndefined()) ? -1 :
      Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(-1);
    if ((deviceId >= 0) && (deviceId < Pa_GetDeviceCount())) {
      inputParameters.device = (PaDeviceIndex) deviceId;
    } else {
      inputParameters.device = Pa_GetDefaultInputDevice();
    }
    if (inputParameters.device == paNoDevice) {
      sprintf(str, "No default input device");
      Nan::ThrowError(str);
      return;
    }
    printf("Input device name is %s.\n", Pa_GetDeviceInfo(inputParameters.device)->name);

    v8Val = Nan::Get(options, Nan::New("channelCount").ToLocalChecked());
    inputParameters.channelCount = Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(2);

    if (inputParameters.channelCount > Pa_GetDeviceInfo(inputParameters.device)->maxInputChannels) {
      Nan::ThrowError("Channel count exceeds maximum number of input channels for device.");
      return;
    }

    v8Val = Nan::Get(options, Nan::New("sampleFormat").ToLocalChecked());
    switch(Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(0)) {
    case 8:
      inputParameters.sampleFormat = paInt8;
      break;
    case 16:
      inputParameters.sampleFormat = paInt16;
      break;
    case 24:
      inputParameters.sampleFormat = paInt24;
      break;
    case 32:
      inputParameters.sampleFormat = paInt32;
      break;
    default:
      Nan::ThrowError("Invalid sampleFormat.");
      return;
    }

    inputParameters.suggestedLatency = 0;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    v8Val = Nan::Get(options, Nan::New("sampleRate").ToLocalChecked());
    sampleRate = Nan::To<int32_t>(v8Val.ToLocalChecked()).FromMaybe(44100);

    err = Pa_OpenStream(
			&stream,
			&inputParameters,
			NULL,//No output
			sampleRate,
			FRAMES_PER_BUFFER,
			0,//No flags being used
			PortAudioCallbackHelper,
			(void *)this);
    if(err != paNoError) {
      sprintf(str, "Could not open stream %s", Pa_GetErrorText(err));
      Nan::ThrowError(str);
      return;
    }

    return;

  }

  AudioInput::~AudioInput(){}

  NAN_METHOD(AudioInput::InputSetCallback){
    AudioInput * obj = Nan::ObjectWrap::Unwrap<AudioInput>(info.Holder());
    v8::Local<v8::Function> callback = info[0].As<v8::Function>();
    obj->pushCallback.Reset(callback);
    obj->enablePush = true;
  }

  NAN_METHOD(AudioInput::InputStreamStop) {
    AudioInput * obj = Nan::ObjectWrap::Unwrap<AudioInput>(info.Holder());
    if (obj->stream != NULL) {
      PaError err = Pa_CloseStream(obj->stream);
      if(err != paNoError) {
	char str[1000];
	sprintf(str, "Could not start stream %d", err);
	Nan::ThrowError(str);
      }
    }

    info.GetReturnValue().SetUndefined();
  }

  NAN_METHOD(AudioInput::InputStreamStart) {
    AudioInput * obj = Nan::ObjectWrap::Unwrap<AudioInput>(info.Holder());
    if (obj->stream != NULL) {
      PaError err = Pa_StartStream(obj->stream);
      if(err != paNoError) {
	char str[1000];
	sprintf(str, "Could not close stream %s", Pa_GetErrorText(err));
	Nan::ThrowError(str);
      }
    }

    info.GetReturnValue().SetUndefined();
  }

  NAN_METHOD(AudioInput::ReadableRead) {
    AudioInput * obj = Nan::ObjectWrap::Unwrap<AudioInput>(info.Holder());
    console.log("THERE");
    //If the buffer is empty return null
    if(obj->bufferStack.size() == 0){
      info.GetReturnValue().SetUndefined();
      console.log("HERE");
      return;
    }
    console.log("HERE"); 
  
    //Calculate memory required to transfer entire buffer stack
    size_t totalMem = obj->bufferStack.front().size();

    /*
      This memory allocation is not freed in this script, as once
      this data is passed into nodejs by Nan responsibility for
      freeing it is passed to nodejs, as per Nan buffer documentation
    */
    char * nanTransferBuffer;
    uv_mutex_lock(&obj->padlock);
    string pulledBuffer = obj->bufferStack.front();
    obj->bufferStack.pop();
    uv_mutex_unlock(&obj->padlock);
    nanTransferBuffer = (char *)calloc(pulledBuffer.size(),sizeof(char));
    memcpy(nanTransferBuffer,pulledBuffer.data(),pulledBuffer.size());

    //Create the Nan object to be returned
    info.GetReturnValue().Set(Nan::NewBuffer(nanTransferBuffer,totalMem).ToLocalChecked());
  }

  NAN_METHOD(AudioInput::ItemsAvailable){
    AudioInput * obj = Nan::ObjectWrap::Unwrap<AudioInput>(info.Holder());
    int toReturn = obj->bufferStack.size();
    info.GetReturnValue().Set(toReturn);
  }

  NAN_METHOD(AudioInput::DisablePush){
    AudioInput * obj = Nan::ObjectWrap::Unwrap<AudioInput>(info.Holder());
    obj->enablePush = false;
  }

  //Push data onto the readable stream on the node thread
  void AudioInput::ReadableCallback(uv_async_t* req) {
    Nan::HandleScope scope;
    if(pushCallback.IsEmpty()){
      char str[1000];
      sprintf(str,"Push callback returned empty");
      Nan::ThrowError(str);
      return;
    }
    if(enablePush){
      Nan::Callback * callback = new Nan::Callback(Nan::New(pushCallback).As<v8::Function>());
      callback->Call(0,0);
    }
  }
  
  //Port audio calls tis every time it has new data to give us
  int AudioInput::nodePortAudioInputCallback(
					const void *inputBuffer,
					void *outputBuffer,
					unsigned long framesPerBuffer,
					const PaStreamCallbackTimeInfo* timeInfo,
					PaStreamCallbackFlags statusFlags) {

    //Calculate size of returned data
    int multiplier = 1;
    switch(sampleFormat) {
    case paInt8:
      multiplier = 1;
      break;
    case paInt16:
      multiplier = 2;
      break;
    case paInt24:
      multiplier = 3;
      break;
    case paInt32:
      multiplier = 4;
      break;
    }

    //Add the frame of audio to the queue
    multiplier = multiplier * channelCount;
    int bytesDelivered = ((int) framesPerBuffer) * multiplier;
    string buffer((char *)inputBuffer,bytesDelivered);
    uv_mutex_lock(&padlock);
    bufferStack.push(buffer);
    uv_mutex_unlock(&padlock);
    //Schedule a callback to nodejs


    uv_async_send(req);
    int ret = 0;
    if(ret < 0){
      cerr << "Got uv async return code of " << ret;
    }
  
    return paContinue;
  }

  NAN_MODULE_INIT(AudioInput::Init) {
    v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
    tpl->SetClassName(Nan::New("AudioInput").ToLocalChecked());
    tpl->InstanceTemplate()->SetInternalFieldCount(1);

    SetPrototypeMethod(tpl, "InputStreamStart", InputStreamStart);
    SetPrototypeMethod(tpl, "InputStreamStop", InputStreamStop);
    SetPrototypeMethod(tpl, "ReadableRead", ReadableRead);
    SetPrototypeMethod(tpl, "ItemsAvailable", ItemsAvailable);
    SetPrototypeMethod(tpl, "InputSetCallback", InputSetCallback);
    SetPrototypeMethod(tpl, "DisablePush", DisablePush);

    constructor().Reset(Nan::GetFunction(tpl).ToLocalChecked());
    Nan::Set(target, Nan::New("AudioInput").ToLocalChecked(),
	     Nan::GetFunction(tpl).ToLocalChecked());
  }

  
}//namespace streampunk
