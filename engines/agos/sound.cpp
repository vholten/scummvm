/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "common/file.h"
#include "common/memstream.h"
#include "common/textconsole.h"
#include "common/util.h"
#include "common/substream.h"

#include "agos/agos.h"
#include "agos/sound.h"

#include "audio/audiostream.h"
#include "audio/decoders/flac.h"
#include "audio/mixer.h"
#include "audio/decoders/mp3.h"
#include "audio/decoders/raw.h"
#include "audio/decoders/voc.h"
#include "audio/decoders/vorbis.h"
#include "audio/decoders/wave.h"

namespace AGOS {

#define SOUND_BIG_ENDIAN true

class BaseSound : Common::NonCopyable {
protected:
	const Common::String _filename;
	uint32 *_offsets;
	Audio::Mixer *_mixer;
	bool _freeOffsets;

	Common::SeekableReadStream *getSoundStream(uint sound) const;
public:
	BaseSound(Audio::Mixer *mixer, const Common::String &filename, uint32 base, bool bigEndian);
	BaseSound(Audio::Mixer *mixer, const Common::String &filename, uint32 *offsets);
	virtual ~BaseSound();

	void playSound(uint sound, Audio::Mixer::SoundType type, Audio::SoundHandle *handle, bool loop, int vol = 0) {
		playSound(sound, sound, type, handle, loop, vol);
	}
	virtual void playSound(uint sound, uint loopSound, Audio::Mixer::SoundType type, Audio::SoundHandle *handle, bool loop, int vol = 0);
	virtual Audio::RewindableAudioStream *makeAudioStream(uint sound) = 0;
};

BaseSound::BaseSound(Audio::Mixer *mixer, const Common::String &filename, uint32 base, bool bigEndian)
	: _mixer(mixer), _filename(filename), _offsets(nullptr) {

	uint res = 0;
	uint32 size;

	Common::File file;
	if (!file.open(_filename))
		error("BaseSound: Could not open file \"%s\"", filename.c_str());

	file.seek(base + sizeof(uint32), SEEK_SET);
	if (bigEndian)
		size = file.readUint32BE();
	else
		size = file.readUint32LE();

	// The Feeble Files uses set amount of voice offsets
	if (size == 0)
		size = 40000;

	res = size / sizeof(uint32);

	_offsets = (uint32 *)malloc(size + sizeof(uint32));
	_freeOffsets = true;

	file.seek(base, SEEK_SET);

	for (uint i = 0; i < res; i++) {
		if (bigEndian)
			_offsets[i] = base + file.readUint32BE();
		else
			_offsets[i] = base + file.readUint32LE();
	}

	_offsets[res] = file.size();
}

BaseSound::BaseSound(Audio::Mixer *mixer, const Common::String &filename, uint32 *offsets)
	: _mixer(mixer), _filename(filename), _offsets(offsets), _freeOffsets(false) {
}

BaseSound::~BaseSound() {
	if (_freeOffsets)
		free(_offsets);
}

Common::SeekableReadStream *BaseSound::getSoundStream(uint sound) const {
	if (_offsets == nullptr)
		return nullptr;

	Common::File *file = new Common::File();
	if (!file->open(_filename)) {
		warning("BaseSound::getSoundStream: Could not open file \"%s\"", _filename.c_str());
		delete file;
		return nullptr;
	}

	int i = 1;
	while (_offsets[sound + i] == _offsets[sound])
		i++;
	uint end;
	if (_offsets[sound + i] > _offsets[sound]) {
		end = _offsets[sound + i];
	} else {
		end = file->size();
	}

	return new Common::SeekableSubReadStream(file, _offsets[sound], end, DisposeAfterUse::YES);
}

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

static void convertVolume(int &vol) {
	// DirectSound was originally used, which specifies volume
	// and panning differently than ScummVM does, using a logarithmic scale
	// rather than a linear one.
	//
	// Volume is a value between -10,000 and 0.
	//
	// In both cases, the -10,000 represents -100 dB. When panning, only
	// one speaker's volume is affected - just like in ScummVM - with
	// negative values affecting the left speaker, and positive values
	// affecting the right speaker. Thus -10,000 means the left speaker is
	// silent.

	int v = CLIP(vol, -10000, 0);
	if (v) {
		vol = (int)((double)Audio::Mixer::kMaxChannelVolume * pow(10.0, (double)v / 2000.0) + 0.5);
	} else {
		vol = Audio::Mixer::kMaxChannelVolume;
	}
}

static void convertPan(int &pan) {
	// DirectSound was originally used, which specifies volume
	// and panning differently than ScummVM does, using a logarithmic scale
	// rather than a linear one.
	//
	// Panning is a value between -10,000 and 10,000.
	//
	// In both cases, the -10,000 represents -100 dB. When panning, only
	// one speaker's volume is affected - just like in ScummVM - with
	// negative values affecting the left speaker, and positive values
	// affecting the right speaker. Thus -10,000 means the left speaker is
	// silent.

	int p = CLIP(pan, -10000, 10000);
	if (p < 0) {
		pan = (int)(255.0 * pow(10.0, (double)p / 2000.0) + 127.5);
	} else if (p > 0) {
		pan = (int)(255.0 * pow(10.0, (double)p / -2000.0) - 127.5);
	} else {
		pan = 0;
	}
}

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

// TODO: Move to a better place?
void BaseSound::playSound(uint sound, uint loopSound, Audio::Mixer::SoundType type, Audio::SoundHandle *handle, bool loop, int vol) {
	convertVolume(vol);
	_mixer->playStream(type, handle, Audio::makeLoopingAudioStream(makeAudioStream(sound), loop ? loopSound : 1), -1, vol);
}

class WavSound : public BaseSound {
public:
	WavSound(Audio::Mixer *mixer, const Common::String &filename, uint32 base = 0)
		: BaseSound(mixer, filename, base, false) {}
	WavSound(Audio::Mixer *mixer, const Common::String &filename, uint32 *offsets) : BaseSound(mixer, filename, offsets) {}
	Audio::RewindableAudioStream *makeAudioStream(uint sound) override;
};

Audio::RewindableAudioStream *WavSound::makeAudioStream(uint sound) {
	Common::SeekableReadStream *tmp = getSoundStream(sound);
	if (!tmp)
		return nullptr;
	return Audio::makeWAVStream(tmp, DisposeAfterUse::YES);
}

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

class VocSound : public BaseSound {
	const byte _flags;
public:
	VocSound(Audio::Mixer *mixer, const Common::String &filename, bool isUnsigned, uint32 base = 0, bool bigEndian = false)
		: BaseSound(mixer, filename, base, bigEndian), _flags(isUnsigned ? Audio::FLAG_UNSIGNED : 0) {}
	Audio::RewindableAudioStream *makeAudioStream(uint sound) override;
};

Audio::RewindableAudioStream *VocSound::makeAudioStream(uint sound) {
	Common::SeekableReadStream *tmp = getSoundStream(sound);
	if (!tmp)
		return nullptr;
	return Audio::makeVOCStream(tmp, _flags, DisposeAfterUse::YES);
}

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

// This class is only used by speech in Simon1 Amiga CD32
class RawSound : public BaseSound {
	const byte _flags;
public:
	RawSound(Audio::Mixer *mixer, const Common::String &filename, bool isUnsigned)
		: BaseSound(mixer, filename, 0, SOUND_BIG_ENDIAN), _flags(isUnsigned ? Audio::FLAG_UNSIGNED : 0) {}
	Audio::RewindableAudioStream *makeAudioStream(uint sound) override;
	void playSound(uint sound, uint loopSound, Audio::Mixer::SoundType type, Audio::SoundHandle *handle, bool loop, int vol = 0) override;
};

Audio::RewindableAudioStream *RawSound::makeAudioStream(uint sound) {
	if (_offsets == nullptr)
		return nullptr;

	Common::File *file = new Common::File();
	if (!file->open(_filename)) {
		warning("RawSound::makeAudioStream: Could not open file \"%s\"", _filename.c_str());
		delete file;
		return nullptr;
	}

	file->seek(_offsets[sound], SEEK_SET);
	uint size = file->readUint32BE();
	return Audio::makeRawStream(new Common::SeekableSubReadStream(file, _offsets[sound] + 4, _offsets[sound] + 4 + size, DisposeAfterUse::YES), 22050, _flags, DisposeAfterUse::YES);
}

void RawSound::playSound(uint sound, uint loopSound, Audio::Mixer::SoundType type, Audio::SoundHandle *handle, bool loop, int vol) {
	// Sound looping and volume are ignored.
	_mixer->playStream(type, handle, makeAudioStream(sound));
}

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

#ifdef USE_MAD
class MP3Sound : public BaseSound {
public:
	MP3Sound(Audio::Mixer *mixer, const Common::String &filename, uint32 base = 0) : BaseSound(mixer, filename, base, false) {}
	Audio::RewindableAudioStream *makeAudioStream(uint sound) override {
		Common::SeekableReadStream *tmp = getSoundStream(sound);
		if (!tmp)
			return nullptr;
		return Audio::makeMP3Stream(tmp, DisposeAfterUse::YES);
	}
};
#endif

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

#ifdef USE_VORBIS
class VorbisSound : public BaseSound {
public:
	VorbisSound(Audio::Mixer *mixer, const Common::String &filename, uint32 base = 0) : BaseSound(mixer, filename, base, false) {}
	Audio::RewindableAudioStream *makeAudioStream(uint sound) override {
		Common::SeekableReadStream *tmp = getSoundStream(sound);
		if (!tmp)
			return nullptr;
		return Audio::makeVorbisStream(tmp, DisposeAfterUse::YES);
	}
};
#endif

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

#ifdef USE_FLAC
class FLACSound : public BaseSound {
public:
	FLACSound(Audio::Mixer *mixer, const Common::String &filename, uint32 base = 0) : BaseSound(mixer, filename, base, false) {}
	Audio::RewindableAudioStream *makeAudioStream(uint sound) override {
		Common::SeekableReadStream *tmp = getSoundStream(sound);
		if (!tmp)
			return nullptr;
		return Audio::makeFLACStream(tmp, DisposeAfterUse::YES);
	}
};
#endif

///////////////////////////////////////////////////////////////////////////////
#pragma mark -

static BaseSound *makeSound(Audio::Mixer *mixer, const Common::String &basename) {
#ifdef USE_FLAC
	if (Common::File::exists(basename + ".fla"))
		return new FLACSound(mixer, basename + ".fla");
#endif
#ifdef USE_VORBIS
	if (Common::File::exists(basename + ".ogg"))
		return new VorbisSound(mixer, basename + ".ogg");
#endif
#ifdef USE_MAD
	if (Common::File::exists(basename + ".mp3"))
		return new MP3Sound(mixer, basename + ".mp3");
#endif
	if (Common::File::exists(basename + ".wav"))
		return new WavSound(mixer, basename + ".wav");
	if (Common::File::exists(basename + ".voc"))
		return new VocSound(mixer, basename + ".voc", true);
	return nullptr;
}


///////////////////////////////////////////////////////////////////////////////
#pragma mark -

Sound::Sound(AGOSEngine *vm, const GameSpecificSettings *gss, Audio::Mixer *mixer)
	: _vm(vm), _mixer(mixer) {
	_voice = nullptr;
	_effects = nullptr;

	_filenums = nullptr;
	_lastVoiceFile = 0;
	_offsets = nullptr;

	_hasEffectsFile = false;
	_hasVoiceFile = false;

	_ambientPlaying = 0;

	_soundQueuePtr = nullptr;
	_soundQueueNum = 0;
	_soundQueueSize = 0;
	_soundQueueFreq = 0;

	if (_vm->getFeatures() & GF_TALKIE) {
		loadVoiceFile(gss);

		if (_vm->getGameType() == GType_SIMON1)
			loadSfxFile(gss);
	}
}

Sound::~Sound() {
	delete _voice;
	delete _effects;

	free(_filenums);
	free(_offsets);
}

void Sound::loadVoiceFile(const GameSpecificSettings *gss) {
	// Game versions which use separate voice files
	if (_hasVoiceFile || _vm->getGameType() == GType_FF || _vm->getGameId() == GID_SIMON1CD32)
		return;

	_voice = makeSound(_mixer, gss->speech_filename);
	_hasVoiceFile = (_voice != nullptr);

	if (_hasVoiceFile)
		return;

	if (_vm->getGameType() == GType_SIMON2) {
		// for simon2 mac/amiga, only read index file
		Common::File file;
		if (file.open("voices.idx")) {
			int end = file.size();
			_filenums = (uint16 *)malloc((end / 6 + 1) * 2);
			_offsets = (uint32 *)malloc((end / 6 + 1 + 1) * 4);

			for (int i = 1; i <= end / 6; i++) {
				_filenums[i] = file.readUint16BE();
				_offsets[i] = file.readUint32BE();
			}
			// We need to add a terminator entry otherwise we get an out of
			// bounds read when the offset table is accessed in
			// BaseSound::getSoundStream.
			_offsets[end / 6 + 1] = 0;

			_hasVoiceFile = true;
			return;
		}
	}

	const bool dataIsUnsigned = true;

	if (Common::File::exists(gss->speech_filename)) {
		_hasVoiceFile = true;
		if (_vm->getGameType() == GType_PP)
			_voice = new WavSound(_mixer, gss->speech_filename);
		else
			_voice = new VocSound(_mixer, gss->speech_filename, dataIsUnsigned);
	}
}

void Sound::loadSfxFile(const GameSpecificSettings *gss) {
	if (_hasEffectsFile)
		return;

	_effects = makeSound(_mixer, gss->effects_filename);
	_hasEffectsFile = (_effects != nullptr);

	if (_hasEffectsFile)
		return;

	const bool dataIsUnsigned = true;

	if (Common::File::exists(gss->effects_filename)) {
		_hasEffectsFile = true;
		_effects = new VocSound(_mixer, gss->effects_filename, dataIsUnsigned);
	}
}

// This method is only used by Simon1 Amiga CD32 & Windows
void Sound::readSfxFile(const Common::String &filename) {
	if (_hasEffectsFile)
		return;

	_mixer->stopHandle(_effectsHandle);

	if (!Common::File::exists(filename)) {
		error("readSfxFile: Can't load sfx file %s", filename.c_str());
	}

	const bool dataIsUnsigned = (_vm->getGameId() != GID_SIMON1CD32);

	delete _effects;
	if (_vm->getGameId() == GID_SIMON1CD32) {
		_effects = new VocSound(_mixer, filename, dataIsUnsigned, 0, SOUND_BIG_ENDIAN);
	} else
		_effects = new WavSound(_mixer, filename);
}

// This method is only used by Simon2
void Sound::loadSfxTable(const char *gameFilename, uint32 base) {
	stopAllSfx();

	delete _effects;
	const bool dataIsUnsigned = true;
	if (_vm->getPlatform() == Common::kPlatformWindows || (_vm->getFeatures() & GF_WAVSFX))
		_effects = new WavSound(_mixer, gameFilename, base);
	else
		_effects = new VocSound(_mixer, gameFilename, dataIsUnsigned, base, false);
}

// This method is only used by Simon1 Amiga CD32
void Sound::readVoiceFile(const Common::String &filename) {
	_mixer->stopHandle(_voiceHandle);

	if (!Common::File::exists(filename))
		error("readVoiceFile: Can't load voice file %s", filename.c_str());

	const bool dataIsUnsigned = false;

	delete _voice;
	_voice = new RawSound(_mixer, filename, dataIsUnsigned);
}

void Sound::playVoice(uint sound) {
	if (_filenums) {
		if (_lastVoiceFile != _filenums[sound]) {
			_mixer->stopHandle(_voiceHandle);

			char filename[16];
			_lastVoiceFile = _filenums[sound];
			Common::sprintf_s(filename, "voices%d.dat", _filenums[sound]);
			if (!Common::File::exists(filename))
				error("playVoice: Can't load voice file %s", filename);

			delete _voice;
			_voice = new WavSound(_mixer, filename, _offsets);
		}
	}

	if (!_voice)
		return;

	_mixer->stopHandle(_voiceHandle);
	if (_vm->getGameType() == GType_PP) {
		if (sound < 11)
			_voice->playSound(sound, sound + 1, Audio::Mixer::kMusicSoundType, &_voiceHandle, true, -1500);
		else
			_voice->playSound(sound, sound, Audio::Mixer::kMusicSoundType, &_voiceHandle, true);
	} else {
		_voice->playSound(sound, Audio::Mixer::kSpeechSoundType, &_voiceHandle, false);
	}
}

void Sound::playEffects(uint sound) {
	if (!_effects)
		return;

	if (_vm->getGameType() == GType_SIMON1)
		_mixer->stopHandle(_effectsHandle);
	_effects->playSound(sound, Audio::Mixer::kSFXSoundType, &_effectsHandle, false);
}

void Sound::playAmbient(uint sound) {
	if (!_effects)
		return;

	if (sound == _ambientPlaying)
		return;

	_ambientPlaying = sound;

	_mixer->stopHandle(_ambientHandle);
	_effects->playSound(sound, Audio::Mixer::kSFXSoundType, &_ambientHandle, true);
}

bool Sound::hasVoice() const {
	return _hasVoiceFile;
}

bool Sound::isSfxActive() const {
	return _mixer->isSoundHandleActive(_effectsHandle);
}

bool Sound::isVoiceActive() const {
	return _mixer->isSoundHandleActive(_voiceHandle);
}

void Sound::stopAllSfx() {
	_mixer->stopHandle(_ambientHandle);
	_mixer->stopHandle(_effectsHandle);
	_mixer->stopHandle(_sfx5Handle);
	_ambientPlaying = 0;
}

void Sound::stopSfx() {
	_mixer->stopHandle(_effectsHandle);
}

void Sound::stopVoice() {
	_mixer->stopHandle(_voiceHandle);
}

void Sound::stopAll() {
	_mixer->stopAll();
	_ambientPlaying = 0;
}

void Sound::effectsMute(bool mute, uint16 effectsVolume) {
	_mixer->setChannelVolume(_effectsHandle, mute ? 0 : effectsVolume);
	_mixer->setChannelVolume(_sfx5Handle, mute ? 0 : effectsVolume);
}

void Sound::ambientMute(bool mute, uint16 effectsVolume) {
	_mixer->setChannelVolume(_ambientHandle, mute ? 0 : effectsVolume);
}

// Personal Nightmare specific
void Sound::handleSoundQueue() {
	if (isSfxActive())
		return;

	_vm->_sampleEnd = 1;

	if (_soundQueuePtr) {
		playRawData(_soundQueuePtr, _soundQueueNum, _soundQueueSize, _soundQueueFreq);

		_vm->_sampleWait = 1;
		_vm->_sampleEnd = 0;
		_soundQueuePtr = nullptr;
		_soundQueueNum = 0;
		_soundQueueSize = 0;
		_soundQueueFreq = 0;
	}
}

void Sound::queueSound(byte *ptr, uint16 sound, uint32 size, uint16 freq) {
	// Only a single sound can be queued
	_soundQueuePtr = ptr;
	_soundQueueNum = sound;
	_soundQueueSize = size;
	_soundQueueFreq = freq;
}

// Elvira 1/2 and Waxworks specific
void Sound::playRawData(byte *soundData, uint sound, uint size, uint freq) {
	byte *buffer = (byte *)malloc(size);
	memcpy(buffer, soundData, size);

	byte flags = 0;
	if (_vm->getPlatform() == Common::kPlatformDOS &&  _vm->getGameId() != GID_ELVIRA2)
		flags = Audio::FLAG_UNSIGNED;

	Audio::AudioStream *stream = Audio::makeRawStream(buffer, size, freq, flags);
	_mixer->playStream(Audio::Mixer::kSFXSoundType, &_effectsHandle, stream);
}

// Feeble Files specific
void Sound::playAmbientData(byte *soundData, uint sound, uint pan, uint vol) {
	if (sound == _ambientPlaying)
		return;

	_ambientPlaying = sound;

	_mixer->stopHandle(_ambientHandle);
	playSoundData(&_ambientHandle, soundData, sound, pan, vol, true);
}

void Sound::playSfxData(byte *soundData, uint sound, uint pan, uint vol) {
	playSoundData(&_effectsHandle, soundData, sound, pan, vol, false);
}

void Sound::playSfx5Data(byte *soundData, uint sound, uint pan, uint vol) {
	_mixer->stopHandle(_sfx5Handle);
	playSoundData(&_sfx5Handle, soundData, sound, pan, vol, true);
}

void Sound::playVoiceData(byte *soundData, uint sound) {
	_mixer->stopHandle(_voiceHandle);
	playSoundData(&_voiceHandle, soundData, sound);
}

void Sound::playSoundData(Audio::SoundHandle *handle, byte *soundData, uint sound, int pan, int vol, bool loop) {
	int size = READ_LE_UINT32(soundData + 4) + 8;
	Common::SeekableReadStream *stream = new Common::MemoryReadStream(soundData, size);
	Audio::RewindableAudioStream *sndStream = Audio::makeWAVStream(stream, DisposeAfterUse::YES);

	convertVolume(vol);
	convertPan(pan);

	_mixer->playStream(Audio::Mixer::kSFXSoundType, handle, Audio::makeLoopingAudioStream(sndStream, loop ? 0 : 1), -1, vol, pan);
}

void Sound::stopSfx5() {
	_mixer->stopHandle(_sfx5Handle);
}

void Sound::switchVoiceFile(const GameSpecificSettings *gss, uint disc) {
	if (_lastVoiceFile == disc)
		return;

	_mixer->stopHandle(_voiceHandle);
	delete _voice;

	_hasVoiceFile = false;
	_lastVoiceFile = disc;

	char filename[16];

	Common::sprintf_s(filename, "%s%u", gss->speech_filename, disc);
	_voice = makeSound(_mixer, filename);
	_hasVoiceFile = (_voice != nullptr);

	if (!_hasVoiceFile)
		error("switchVoiceFile: Can't load voice file %s", filename);
}

} // End of namespace AGOS
