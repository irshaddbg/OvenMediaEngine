#include "mpegtspush_stream.h"

#include <regex>

#include "base/publisher/application.h"
#include "base/publisher/stream.h"
#include "mpegtspush_application.h"
#include "mpegtspush_private.h"

std::shared_ptr<MpegtsPushStream> MpegtsPushStream::Create(const std::shared_ptr<pub::Application> application,
													   const info::Stream &info)
{
	auto stream = std::make_shared<MpegtsPushStream>(application, info);
	return stream;
}

MpegtsPushStream::MpegtsPushStream(const std::shared_ptr<pub::Application> application,
							   const info::Stream &info)
	: Stream(application, info)
{
}

MpegtsPushStream::~MpegtsPushStream()
{
	logtd("MpegtsPushStream(%s/%s) has been terminated finally",
		  GetApplicationName(), GetName().CStr());
}

bool MpegtsPushStream::Start()
{
	if (GetState() != Stream::State::CREATED)
	{
		return false;
	}

	if (!CreateStreamWorker(2))
	{
		return false;
	}

	logtd("MpegtsPushStream(%ld) has been started", GetId());

	return Stream::Start();
}

bool MpegtsPushStream::Stop()
{
	logtd("MpegtsPushStream(%u) has been stopped", GetId());
	
	if (GetState() != Stream::State::STARTED)
	{
		return false;
	}

	return Stream::Stop();
}

void MpegtsPushStream::SendFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	auto stream_packet = std::make_any<std::shared_ptr<MediaPacket>>(media_packet);

	BroadcastPacket(stream_packet);

	MonitorInstance->IncreaseBytesOut(*pub::Stream::GetSharedPtrAs<info::Stream>(), PublisherType::MpegtsPush, media_packet->GetData()->GetLength() * GetSessionCount());
}

void MpegtsPushStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() != Stream::State::STARTED)
	{
		return;
	}

	SendFrame(media_packet);
}

void MpegtsPushStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() != Stream::State::STARTED)
	{
		return;
	}

	SendFrame(media_packet);
}

std::shared_ptr<pub::Session> MpegtsPushStream::CreatePushSession(std::shared_ptr<info::Push> &push)
{
	auto session = std::static_pointer_cast<pub::Session>(MpegtsPushSession::Create(GetApplication(), GetSharedPtrAs<pub::Stream>(), this->IssueUniqueSessionId(), push));
	if (session == nullptr)
	{
		logte("Internal Error : Cannot create session");
		return nullptr;
	}

	AddSession(session);

	return session; 
}

