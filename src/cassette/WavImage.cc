// $Id$

#include "WavImage.hh"
#include "FileContext.hh"
#include "File.hh"


WavImage::WavImage(FileContext *context, const string &fileName)
	: audioLength(0), audioBuffer(0)
{
	// TODO throw exceptions instead of PRT_ERROR
	File file(context->resolve(fileName));
	const char* name = file.getLocalName().c_str();
	if (SDL_LoadWAV(name, &audioSpec, &audioBuffer, &audioLength) == NULL) {
		string msg = string("CassettePlayer error: ") + SDL_GetError();
		throw MSXException(msg);
	}
	if (audioSpec.format != AUDIO_S16) {
		// TODO convert sample
		throw MSXException("CassettePlayer error: unsupported WAV format");
	}
}

WavImage::~WavImage()
{
	if (audioBuffer) {
		SDL_FreeWAV(audioBuffer);
	}
}

short WavImage::getSampleAt(float pos)
{
	unsigned index = (unsigned)(pos * audioSpec.freq);
	if (index < (audioLength / 2)) {
		return ((short*)audioBuffer)[index];
	} else {
		return 0;
	}
}
