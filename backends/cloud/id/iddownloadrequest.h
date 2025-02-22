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

#ifndef BACKENDS_CLOUD_ID_IDDOWNLOADREQUEST_H
#define BACKENDS_CLOUD_ID_IDDOWNLOADREQUEST_H

#include "backends/cloud/storage.h"
#include "backends/networking/curl/request.h"
#include "common/callback.h"

namespace Cloud {
namespace Id {

class IdStorage;

class IdDownloadRequest: public Networking::Request {
	Common::String _requestedFile, _requestedLocalFile;
	IdStorage *_storage;
	Storage::BoolCallback _boolCallback;
	Request *_workingRequest;
	bool _ignoreCallback;

	void start();
	void idResolvedCallback(const Storage::UploadResponse &response);
	void idResolveFailedCallback(const Networking::ErrorResponse &error);
	void downloadCallback(const Storage::BoolResponse &response);
	void downloadErrorCallback(const Networking::ErrorResponse &error);
	void finishDownload(bool success);
public:
	IdDownloadRequest(IdStorage *storage, const Common::String &remotePath, const Common::String &localPath, Storage::BoolCallback cb, Networking::ErrorCallback ecb);
	~IdDownloadRequest() override;

	void handle() override;
	void restart() override;

	/** Returns a number in range [0, 1], where 1 is "complete". */
	double getProgress() const;
};

} // End of namespace Id
} // End of namespace Cloud

#endif
