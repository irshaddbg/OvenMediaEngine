#include "rtmp_writer.h"

#include <modules/bitstream/aac/aac_converter.h>
#include <modules/bitstream/nalu/nal_stream_converter.h>

#include "private.h"

/* 
	[Test Code]

	_writer = RtmpWriter::Create();
	_writer->SetPath("/tmp/output.ts");

	for(auto &track_item : _tracks)
	{
		auto &track = track_item.second;

		if( track->GetCodecId() == cmn::MediaCodecId::Opus )
			continue;

		auto quality = RtmpTrackInfo::Create();

		quality->SetCodecId( track->GetCodecId() );
		quality->SetBitrate( track->GetBitrate() );
		quality->SetTimeBase( track->GetTimeBase() );
		quality->SetWidth( track->GetWidth() );
		quality->SetHeight( track->GetHeight() );
		quality->SetSample( track->GetSample() );
		quality->SetChannel( track->GetChannel() );

		_writer->AddTrack(track->GetMediaType(), track->GetId(), quality);
	}

	_writer->Start();

	while(1)
	{
		_writer->PutData(
			media_packet->GetTrackId(), 
			media_packet->GetPts(),
			media_packet->GetDts(), 
			media_packet->GetFlag(), 
			media_packet->GetData());
	}

	_writer->Stop();

*/

std::shared_ptr<RtmpWriter> RtmpWriter::Create()
{
	auto object = std::make_shared<RtmpWriter>();

	return object;
}

RtmpWriter::RtmpWriter()
	: _format_context(nullptr)
{
}

RtmpWriter::~RtmpWriter()
{
	Stop();
}

bool RtmpWriter::SetPath(const ov::String path, const ov::String format)
{
	std::unique_lock<std::mutex> mlock(_lock);

	if (path.IsEmpty() == true)
	{
		logte("The path is empty");
		return false;
	}

	_path = path;

	// Release the allcated context;
	if (_format_context != nullptr)
	{
		avformat_close_input(&_format_context);
		avformat_free_context(_format_context);
		_format_context = nullptr;
	}

	int error = avformat_alloc_output_context2(&_format_context, nullptr, (format != nullptr)?format.CStr():nullptr, path.CStr());
	if (error < 0)
	{
		char errbuf[256];
		av_strerror(error, errbuf, sizeof(errbuf));

		logte("Could not create output context. error(%d:%s), path(%s)", error, errbuf, path.CStr());

		return false;
	}

	return true;
}

ov::String RtmpWriter::GetPath()
{
	return _path;
}

bool RtmpWriter::Start()
{
	std::unique_lock<std::mutex> mlock(_lock);

	AVDictionary *options = nullptr;

	// Compatibility with specific RTMP servers

	// tc_url : rtmp://[host]:[port]/[app_name]
	ov::String tc_url = ov::String(_format_context->url);
	tc_url = tc_url.Substring(0, tc_url.IndexOfRev('/'));
	av_dict_set(&options, "rtmp_tcurl", tc_url.CStr(), 0);
	av_dict_set(&options, "fflags", "flush_packets", 0);
	av_dict_set(&options, "rtmp_flashver", "FMLE/3.0 (compatible; FMSc/1.0)", 0);

	if (!(_format_context->oformat->flags & AVFMT_NOFILE))
	{
		int error = avio_open2(&_format_context->pb, _format_context->url, AVIO_FLAG_WRITE, nullptr, &options);
		if (error < 0)
		{
			char errbuf[256];
			av_strerror(error, errbuf, sizeof(errbuf));

			logte("Error opening file. error(%d:%s), url(%s)", error, errbuf, _format_context->url);

			return false;
		}
	}

	if (avformat_write_header(_format_context, nullptr) < 0)
	{
		logte("Could not create header");
		return false;
	}

	av_dump_format(_format_context, 0, _format_context->url, 1);

	if (_format_context->oformat != nullptr)
	{
		[[maybe_unused]] auto oformat = _format_context->oformat;
		logtd("name : %s", oformat->name);
		logtd("long_name : %s", oformat->long_name);
		logtd("mime_type : %s", oformat->mime_type);
		logtd("audio_codec : %d", oformat->audio_codec);
		logtd("video_codec : %d", oformat->video_codec);
	}

	return true;
}

bool RtmpWriter::Stop()
{
	std::unique_lock<std::mutex> mlock(_lock);

	if (_format_context != nullptr)
	{
		if (_format_context->pb != nullptr)
		{
			avformat_close_input(&_format_context);			
		}

		avformat_free_context(_format_context);

		_format_context = nullptr;
	}

	return true;
}

bool RtmpWriter::AddTrack(cmn::MediaType media_type, int32_t track_id, std::shared_ptr<RtmpTrackInfo> track_info)
{
	std::unique_lock<std::mutex> mlock(_lock);

	AVStream *stream = nullptr;

	//Stream #0:0(und): Video: h264 (Constrained Baseline) ([7][0][0][0] / 0x0007), yuv420p, 640x360 [SAR 1:1 DAR 16:9], q=2-31, 683 kb/s, 24 fps, 24 tbr, 1k tbn, 90k tbc (default)
	//Stream #0:0:     Video: h264, 1 reference frame ([7][0][0][0] / 0x0007), yuv420p, 1920x1080 (0x0) [SAR 1:1 DAR 16:9], 0/1, q=2-31, 2500 kb/s
	// 	[12-02 17:03:03.131] I 25357 FFmpeg | third_parties.cpp:118  |     Stream #0:0
	// [12-02 17:03:03.131] I 25357 FFmpeg | third_parties.cpp:118  | : Video: h264 (Constrained Baseline), 1 reference frame ([7][0][0][0] / 0x0007), yuv420p, 1920x1080 [SAR 1:1 DAR 16:9], 0/1, q=2-31, 2000 kb/s, 1k tbn, 30 tbc
	switch (media_type)
	{
		case cmn::MediaType::Video: {
			stream = avformat_new_stream(_format_context, nullptr);
			AVCodecParameters *codecpar = stream->codecpar;

			codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
			codecpar->codec_id =
				(track_info->GetCodecId() == cmn::MediaCodecId::H264) ? AV_CODEC_ID_H264 : (track_info->GetCodecId() == cmn::MediaCodecId::H265) ? AV_CODEC_ID_H265
																					   : (track_info->GetCodecId() == cmn::MediaCodecId::Vp8)	 ? AV_CODEC_ID_VP8
																					   : (track_info->GetCodecId() == cmn::MediaCodecId::Vp9)	 ? AV_CODEC_ID_VP9
																																				 : AV_CODEC_ID_NONE;

			codecpar->codec_tag = 0;
			codecpar->bit_rate = track_info->GetBitrate();
			codecpar->width = track_info->GetWidth();
			codecpar->height = track_info->GetHeight();
			codecpar->format = AV_PIX_FMT_YUV420P;
			codecpar->sample_aspect_ratio = AVRational{1, 1};

			// set extradata for avc_decoder_configuration_record
			if (track_info->GetExtradata() != nullptr)
			{
				codecpar->extradata_size = track_info->GetExtradata()->GetLength();
				codecpar->extradata = (uint8_t *)av_malloc(codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				memset(codecpar->extradata, 0, codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				memcpy(codecpar->extradata, track_info->GetExtradata()->GetDataAs<uint8_t>(), codecpar->extradata_size);
			}
			else
			{
				logtw("there is no avc decoder configuration");
			}

			stream->time_base = AVRational{track_info->GetTimeBase().GetNum(), track_info->GetTimeBase().GetDen()};

			_track_map[track_id] = stream->index;
			_trackinfo_map[track_id] = track_info;
		}
		break;

		case cmn::MediaType::Audio: {
			stream = avformat_new_stream(_format_context, nullptr);
			AVCodecParameters *codecpar = stream->codecpar;

			codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
			codecpar->codec_id = 
				(track_info->GetCodecId() == cmn::MediaCodecId::Aac) ? AV_CODEC_ID_AAC : 
				(track_info->GetCodecId() == cmn::MediaCodecId::Mp3) ? AV_CODEC_ID_MP3 : 
				(track_info->GetCodecId() == cmn::MediaCodecId::Opus) ? AV_CODEC_ID_OPUS : AV_CODEC_ID_NONE;
			codecpar->bit_rate = track_info->GetBitrate();
			codecpar->channels = static_cast<int>(track_info->GetChannel().GetCounts());
			codecpar->channel_layout = 
				(track_info->GetChannel().GetLayout() == cmn::AudioChannel::Layout::LayoutMono) ? AV_CH_LAYOUT_MONO : 
				(track_info->GetChannel().GetLayout() == cmn::AudioChannel::Layout::LayoutStereo) ? AV_CH_LAYOUT_STEREO : 0;	 // <- Unknown
			codecpar->sample_rate = track_info->GetSample().GetRateNum();
			codecpar->frame_size = 1024;  // TODO: Need to Frame Size
			codecpar->codec_tag = 0;

			// set extradata for aac_specific_config
			if (track_info->GetExtradata() != nullptr)
			{
				codecpar->extradata_size = track_info->GetExtradata()->GetLength();
				codecpar->extradata = (uint8_t *)av_malloc(codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				memset(codecpar->extradata, 0, codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				memcpy(codecpar->extradata, track_info->GetExtradata()->GetDataAs<uint8_t>(), codecpar->extradata_size);
			}

			stream->time_base = AVRational{track_info->GetTimeBase().GetNum(), track_info->GetTimeBase().GetDen()};

			_track_map[track_id] = stream->index;
			_trackinfo_map[track_id] = track_info;
		}
		break;

		default: {
			logtw("This media type is not supported. media_type(%d)", media_type);
			return false;
		}
		break;
	}

	return true;
}

// MP4
//	- H264 : AnnexB bitstream
// 	- AAC : ASC(Audio Specific Config) bitstream

bool RtmpWriter::PutData(int32_t track_id, int64_t pts, int64_t dts, MediaPacketFlag flag, cmn::BitstreamFormat format, std::shared_ptr<ov::Data> &data)
{
	std::unique_lock<std::mutex> mlock(_lock);

	if (_format_context == nullptr)
		return false;

	// Find AVStream and Index;
	int stream_index = 0;

	auto iter = _track_map.find(track_id);
	if (iter == _track_map.end())
	{
		// Without a track, it's not an error. Ignore.
		return true;
	}
	stream_index = iter->second;

	AVStream *stream = _format_context->streams[stream_index];
	if (stream == nullptr)
	{
		logtw("There is no stream");
		return false;
	}

	// Find Ouput Track Info
	auto track_info = _trackinfo_map[track_id];

	// Make avpacket
	AVPacket av_packet = {0};

	av_packet.stream_index = stream_index;
	av_packet.flags = (flag == MediaPacketFlag::Key) ? AV_PKT_FLAG_KEY : 0;
	av_packet.pts = av_rescale_q(pts, AVRational{track_info->GetTimeBase().GetNum(), track_info->GetTimeBase().GetDen()}, stream->time_base);
	av_packet.dts = av_rescale_q(dts, AVRational{track_info->GetTimeBase().GetNum(), track_info->GetTimeBase().GetDen()}, stream->time_base);

	std::shared_ptr<const ov::Data> cdata = data;
	std::vector<size_t> length_list;

	if (strcmp(_format_context->oformat->name, "flv") == 0)
	{
		switch (format)
		{
			case cmn::BitstreamFormat::H264_AVCC:
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			case cmn::BitstreamFormat::H264_ANNEXB:
				cdata = NalStreamConverter::ConvertAnnexbToXvcc(cdata);
				if (cdata == nullptr)
				{
					logtw("Failed to convert annexb to avcc");
					return false;
				}
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			case cmn::BitstreamFormat::AAC_RAW:
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			case cmn::BitstreamFormat::AAC_ADTS:
				cdata = AacConverter::ConvertAdtsToRaw(cdata, &length_list);
				if (cdata == nullptr)
				{
					logtw("Failed to convert adts to raw");
					return false;
				}
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			default:
				// Unsupported bitstream foramt
				return false;
		}
	}
	else if (strcmp(_format_context->oformat->name, "mp4") == 0)
	{
		switch (format)
		{
			case cmn::BitstreamFormat::AAC_RAW:
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			case cmn::BitstreamFormat::AAC_ADTS:
				cdata = AacConverter::ConvertAdtsToRaw(cdata, &length_list);
				if (cdata == nullptr)
				{
					logtw("Failed to convert adts to raw");
					return false;
				}
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			case cmn::BitstreamFormat::H264_ANNEXB:
				[[fallthrough]];
			case cmn::BitstreamFormat::H264_AVCC:
				av_packet.size = cdata->GetLength();
				av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
				break;

			default:
				// Unsupported bitstream foramt
				return false;
		}
	}
	else if (strcmp(_format_context->oformat->name, "mpegts") == 0)
	{
		av_packet.size = cdata->GetLength();
		av_packet.data = (uint8_t *)cdata->GetDataAs<uint8_t>();
	}

	int error = av_interleaved_write_frame(_format_context, &av_packet);
	if (error != 0)
	{
		char errbuf[256];
		av_strerror(error, errbuf, sizeof(errbuf));

		logte("Send packet error(%d:%s)", error, errbuf);
		return false;
	}

	return true;
}
