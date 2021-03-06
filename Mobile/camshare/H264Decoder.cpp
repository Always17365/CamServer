/*
 * H264Decoder.cpp
 *
 *  Created on: 2016年10月28日
 *      Author: max
 */

#include "H264Decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <common/KLog.h>
#include <common/Arithmetic.h>

H264Decoder::H264Decoder() {
	// TODO Auto-generated constructor stub
	mCodec = NULL;
	mContext = NULL;
	mGotPicture = 0;
	mLen = 0;
	mFilePath = "";
    // 解码的格式
    mDecoderFormat = AV_PIX_FMT_RGB565;
}

H264Decoder::~H264Decoder() {
	// TODO Auto-generated destructor stub
}

void H264Decoder::GobalInit() {
	avcodec_register_all();
}

bool H264Decoder::Create() {
    FileLog("CamshareClient", "H264Decoder::Create( this : %p, avcodec_version : %d )", this, avcodec_version());
    
    avcodec_register_all();
	mCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if ( !mCodec ) {
		// codec not found
		FileLog("CamshareClient",
				"H264Decoder::Create( "
				"this : %p, "
				"fail, "
				"codec not found "
				")",
				this
				);

		return false;
	}

	mContext = avcodec_alloc_context3(mCodec);

	if (mCodec->capabilities & CODEC_CAP_TRUNCATED)
		mContext->flags |= CODEC_FLAG_TRUNCATED; /* we do not send complete frames */

	/* For some codecs, such as msmpeg4 and mpeg4, width and height
	 MUST be initialized there because this information is not
	 available in the bitstream. */

	/* open it */
	if (avcodec_open2(mContext, mCodec, NULL) < 0) {
		// could not open codec
		FileLog("CamshareClient",
				"H264Decoder::Create( "
				"this : %p, "
				"fail, "
				"could not open codec "
				")",
				this
				);

		return false;
	}

	FileLog("CamshareClient",
			"H264Decoder::Create( "
			"this : %p, "
			"success "
			")",
			this
			);

	return true;
}

void H264Decoder::Destroy() {
	FileLog("CamshareClient",
			"H264Decoder::Destroy(start "
			"this : %p, "
			")",
			this
			);
	if( mContext ) {
		avcodec_close(mContext);
		mContext = NULL;
	}

	mCodec = NULL;
	FileLog("CamshareClient",
			"H264Decoder::Destroy(end "
			"this : %p, "
			")",
			this
			);
}

bool H264Decoder::DecodeFrame(
		const char* srcData,
		unsigned int dataLen,
		char* frame,
		int& frameLen,
		int& width,
		int& height
		) {
	bool bFlag = false;

	frameLen = 0;
	width = 0;
	height = 0;

	char* h264frame = NULL;
	unsigned int h264FrameLen = 0;

	FileLog("CamshareClient",
			"H264Decoder::DecodeFrame( "
			"this : %p, "
			"######################## Frame ########################"
			")",
			this
			);

	if( mRtp2H264VideoTransfer.GetFrame(srcData, dataLen, &h264frame, h264FrameLen) ) {
		AVPacket avpkt = {0};
		av_init_packet(&avpkt);
		avpkt.data = (uint8_t *)h264frame;
		avpkt.size = h264FrameLen;

		FileLog("CamshareClient",
				"H264Decoder::DecodeFrame( "
				"[Got Frame], "
				"this : %p, "
				"h264FrameLen : %u "
				")",
				this,
				h264FrameLen
				);

//		// 录制文件
//		if( mFilePath.length() > 0 ) {
//			FILE* file = fopen(mFilePath.c_str(), "a+b");
//		    if( file != NULL ) {
//		    	int iLen = -1;
//		    	fseek(file, 0L, SEEK_END);
//				iLen = fwrite(h264frame, 1, h264FrameLen, file);
//		    	fclose(file);
//		    }
//		}

		AVFrame* pictureFrame = av_frame_alloc();
	    int useLen = avcodec_decode_video2(mContext, pictureFrame, &mGotPicture, &avpkt);
		if (mGotPicture) {

			AVFrame *rgbFrame = av_frame_alloc();
			int numBytes = avpicture_get_size(mDecoderFormat, mContext->width, mContext->height);
			uint8_t* buffer = (uint8_t *)av_malloc(numBytes);
			avpicture_fill((AVPicture *)rgbFrame, (uint8_t *)buffer, mDecoderFormat,
					mContext->width, mContext->height);
			struct SwsContext *img_convert_ctx = sws_getContext(mContext->width, mContext->height, mContext->pix_fmt,
					mContext->width, mContext->height, mDecoderFormat, SWS_BICUBIC, NULL, NULL, NULL);

			sws_scale(img_convert_ctx,
					(const uint8_t* const *)pictureFrame->data,
					pictureFrame->linesize, 0, mContext->height, rgbFrame->data,
					rgbFrame->linesize);

			frameLen = numBytes;
			memcpy((void *)frame, (const void *)rgbFrame->data[0], frameLen);
            
			width = mContext->width;
			height = mContext->height;

			bFlag = true;

			FileLog("CamshareClient",
					"H264Decoder::DecodeFrame( "
					"[Got Picture], "
					"this : %p, "
					"mContext : %p, "
					"width : %d, "
					"height : %d, "
					"useLen : %d, "
					"frameLen : %d, "
					"pix_fmt : %d "
					")",
					this,
					mContext,
					mContext->width,
					mContext->height,
					useLen,
					frameLen,
					mContext->pix_fmt
					);

			av_free(buffer);
			av_free(rgbFrame);
			sws_freeContext(img_convert_ctx);
		}
		else
		{
			FileLog("CamshareClient",
					"H264Decoder::DecodeFrame( "
					"mGotPicture:%d"
					")",
					mGotPicture
					);
		}
		av_free(pictureFrame);

	    // Release video buffer
	    mRtp2H264VideoTransfer.DestroyVideoBuffer();
	}

	FileLog("CamshareClient",
			"H264Decoder::DecodeFrame( end"
			"this : %p, "
			"######################## Frame ########################"
			")",
			this
			);
	return bFlag;
}

void H264Decoder::SetRecordFile(const string& filePath) {
	mFilePath = filePath;
	FileLog("CamshareClient",
			"H264Decoder::SetRecordFile( "
			"this : %p, "
			"filePath : %s "
			")",
			this,
			filePath.c_str()
			);
}

// 设置解码类型
bool H264Decoder::SetDecoderVideoFormat(camshare::VIDEO_FORMATE_TYPE type)
{
    bool result = false;
    switch (type) {
        case camshare::VIDEO_FORMATE_RGB565:
        {
            mDecoderFormat = AV_PIX_FMT_RGB565;
            result = true;
        }
            break;
        case camshare::VIDEO_FORMATE_RGB24:
        {
            mDecoderFormat = AV_PIX_FMT_RGB24;
            result = true;
        }
            break;
        case camshare::VIDEO_FORMATE_YUV420P:
        {
            mDecoderFormat = AV_PIX_FMT_YUV420P;
            result = true;
        }
            break;
            
        default:
            break;
    }

    return result;
}

