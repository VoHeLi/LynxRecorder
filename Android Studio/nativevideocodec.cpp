#include <opencv2/opencv.hpp>
#include <jni.h>
#include <stdio.h>

#include "media/NdkMediaCrypto.h"
#include "media/NdkMediaCodec.h"
#include "media/NdkMediaError.h"
#include "media/NdkMediaFormat.h"
#include "media/NdkMediaMuxer.h"
#include "media/NdkMediaExtractor.h"

#include <OMXAL/OpenMAXAL.h>
#include <OMXAL/OpenMAXAL_Android.h>
#include <android/log.h>

using std::string;

std::string mFilename;
int mFPS;
cv::Size mSize;
bool isRunning;

AMediaCodec* mEncoder;
AMediaMuxer* mMuxer;
AMediaCodecBufferInfo mBufferInfo;
int mTrackIndex;
bool mMuxerStarted;
const static int TIMEOUT_USEC = 10000;
int mFrameCounter;

#define  LOG_TAG    "NDK Video Encoder"

long long computePresentationTimeNsec() {
    mFrameCounter++;
    double timePerFrame = 1000000.0/mFPS;
    return static_cast<long long>(mFrameCounter*timePerFrame);
}


void ReleaseNativeCodecEncoder() {
    //__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "releasing encoder objects");
    if (mEncoder != nullptr) {
        AMediaCodec_stop(mEncoder);
    }

    if (mMuxer != nullptr) {
        AMediaMuxer_stop(mMuxer);
    }

    if (mEncoder != nullptr) {
        AMediaCodec_delete(mEncoder);
        mEncoder = nullptr;
    }

    if (mMuxer != nullptr) {
        AMediaMuxer_delete(mMuxer);
        mMuxer = nullptr;
    }

    isRunning = false;
}

extern "C" {
    JNIEXPORT void JNICALL
    InitNativeCodecEncoder(const char* filename, int fps, int rows, int cols) //TODO check this std::string
    {
        mFilename = std::string(filename);
        mFPS = fps;
        //mSize = size;
        mSize = cv::Size(rows, cols);
        isRunning = false;
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Encoder Initialized!");
    }

    JNIEXPORT void JNICALL
    PrepareNativeCodecEncoder(){
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_WIDTH,mSize.width);
        AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_HEIGHT,mSize.height);

        AMediaFormat_setString(format,AMEDIAFORMAT_KEY_MIME,"video/avc"); // H.264 Advanced Video Coding
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 21); // #21 COLOR_FormatYUV420SemiPlanar (NV12)
        AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_BIT_RATE,500000);
        AMediaFormat_setFloat(format,AMEDIAFORMAT_KEY_FRAME_RATE,mFPS);
        AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_I_FRAME_INTERVAL,5);


        //AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_STRIDE,mSize.width);
        //AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_M  AX_WIDTH,mSize.width);
        //AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_MAX_HEIGHT,mSize.height);

        mEncoder = AMediaCodec_createEncoderByType("video/avc");
        if(mEncoder == nullptr){
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unable to create encoder");
        }


        media_status_t err = AMediaCodec_configure(mEncoder, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
        if(err != AMEDIA_OK){
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", "Error occurred (1) : "/*+err*/);
        }

        err = AMediaCodec_start(mEncoder);
        if(err != AMEDIA_OK){
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", "Error occurred (2) : "/*+ + err*/);
        }

        if(err != AMEDIA_OK){
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", "Error occurred (3) : "/*+ + err*/);
        }


        FILE* outFile = fopen(mFilename.c_str(), "w+");
        if(outFile == nullptr){
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", ("Cannot open file: " + mFilename).c_str());
        }
        else{
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", ("Writing video to file:" + mFilename).c_str());
        }

        // Create a MediaMuxer.  We can't add the video track and start() the muxer here,
        // because our MediaFormat doesn't have the Magic Goodies.  These can only be
        // obtained from the encoder after it has started processing data.
        //
        // We're not actually interested in multiplexing audio.  We just want to convert
        // the raw H.264 elementary stream we get from MediaCodec into a .mp4 file.

        //int fileno(FILE *stream)

        mMuxer = AMediaMuxer_new(fileno(outFile), AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);

        if(mMuxer == nullptr){
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Unable to create Muxer");
        }

        mTrackIndex = -1;
        mMuxerStarted = false;
        mFrameCounter = 0;
        isRunning = true;
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Encoder ready!");
    }

    JNIEXPORT void JNICALL
    DrainNativeCodecEncoder(bool endOfStream) {

        if (endOfStream) {
            __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "Draining encoder to EOS");
            // only API >= 26
            // Send an empty frame with the end-of-stream flag set.
            // AMediaCodec_signalEndOfInputStream();
            // Instead, we construct that frame manually.
        }




        while (true) {
            ssize_t encoderStatus = AMediaCodec_dequeueOutputBuffer(mEncoder, &mBufferInfo, TIMEOUT_USEC);


            if (encoderStatus == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                // no output available yet
                if (!endOfStream) {
                    return;
                    //break;      // out of while
                }
                if(endOfStream){
                    //__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "no output available, spinning to await EOS");
                    return;
                }

            } else if (encoderStatus == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
                // not expected for an encoder
            } else if (encoderStatus == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                // should happen before receiving buffers, and should only happen once
                if (mMuxerStarted) {
                   __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "ERROR: format changed twice");
                }
                AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mEncoder);

                if(newFormat == nullptr){
                    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Unable to set new format.");
                }

                __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", (string("encoder output format changed: ") + string(AMediaFormat_toString(newFormat))).c_str());

                // now that we have the Magic Goodies, start the muxer
                mTrackIndex = AMediaMuxer_addTrack(mMuxer, newFormat);
                media_status_t err = AMediaMuxer_start(mMuxer);

                if(err != AMEDIA_OK){
                    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", "Error occurred (10) : " /*+ err*/);
                }

                mMuxerStarted = true;
            } else if (encoderStatus < 0) {
                __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s", (string("unexpected result from encoder.dequeueOutputBuffer: ") + string(
                        reinterpret_cast<const char *>(encoderStatus))).c_str());
                // let's ignore it
            } else {

                size_t out_size;
                uint8_t* encodedData = AMediaCodec_getOutputBuffer(mEncoder, encoderStatus, &out_size);

                if(out_size <= 0){
                    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Encoded data of size 0.");
                }

                if (encodedData == nullptr) {
                    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s", (string("encoderOutputBuffer ") + string(
                            reinterpret_cast<const char *>(encoderStatus)) + " was null").c_str());
                }


                if ((mBufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0) {
                    // The codec config data was pulled out and fed to the muxer when we got
                    // the INFO_OUTPUT_FORMAT_CHANGED status.  Ignore it.
                    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "ignoring BUFFER_FLAG_CODEC_CONFIG");
                    mBufferInfo.size = 0;
                }

                if (mBufferInfo.size != 0) {
                    if (!mMuxerStarted) {
                        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "muxer hasn't started");
                    }


                    // adjust the ByteBuffer values to match BufferInfo (not needed?)
                    //encodedData.position(mBufferInfo.offset);
                    //encodedData.limit(mBufferInfo.offset + mBufferInfo.size);

                    AMediaMuxer_writeSampleData(mMuxer, mTrackIndex, encodedData, &mBufferInfo);
                    //qDebug() << "sent " + QString::number(mBufferInfo.size) + " bytes to muxer";
                }
                else{
                    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s", "mBufferInfo empty " /*+ mBufferInfo.size*/);
                }

                AMediaCodec_releaseOutputBuffer(mEncoder, encoderStatus, false);

                if ((mBufferInfo.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) {
                    if (!endOfStream) {
                        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "reached end of stream unexpectedly");
                    } else {
                       __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "end of stream reached");
                    }
                    break;      // out of while
                }
            }
        }
    }

    JNIEXPORT bool JNICALL
    WriteNativeCodecEncoder(int rows, int cols, void* data/*, const long long timestamp*/){
        // Feed any pending encoder output into the muxer.
        DrainNativeCodecEncoder(false);

        cv::Mat mat = cv::Mat(rows, cols, CV_8UC4, data);

        if(mat.empty()) return false;

        // Generate a new frame of input.

        /**
                      * Get the index of the next available input buffer. An app will typically use this with
                      * getInputBuffer() to get a pointer to the buffer, then copy the data to be encoded or decoded
                      * into the buffer before passing it to the codec.
                      */
        ssize_t inBufferIdx = AMediaCodec_dequeueInputBuffer(mEncoder, TIMEOUT_USEC);

        /**
                      * Get an input buffer. The specified buffer index must have been previously obtained from
                      * dequeueInputBuffer, and not yet queued.
                      */
        size_t out_size;
        uint8_t* inBuffer = AMediaCodec_getInputBuffer(mEncoder, inBufferIdx, &out_size);

        // Make sure the image is colorful (later on we can try encoding grayscale somehow...)
        cv::Mat colorImg(rows*3/2, cols, CV_8UC1, inBuffer);

        //cv::cvtColor(mat, colorImg, cv::CV_BGR2YUV_I420); // COLOR_FormatYUV420SemiPlanar
        cv::cvtColor(mat, colorImg, cv::COLOR_RGBA2YUV_I420);


        //***YUV420SP FIX
        /*cv::Mat uvMat(rows*1/4, cols, CV_8UC2, inBuffer); //WRONG

        cv::Mat uMat(rows/4, cols, CV_8UC1);
        cv::Mat vMat(rows/4, cols, CV_8UC1);

        cv::Mat colorMats[] = {uMat, vMat};

        cv::split(uvMat, colorMats);

        memcpy(&colorImg + rows*cols, uMat.data, rows*cols*1/4);
        memcpy(&colorImg + rows*cols*5/4, vMat.data, rows*cols*1/4);*/

        cv::Mat uMat(rows/4, cols, CV_8UC1, inBuffer+rows*cols);
        cv::Mat vMat(rows/4, cols, CV_8UC1, inBuffer+(rows*cols)*5/4);

        cv::Mat uvMats[] = {uMat, vMat};

        cv::Mat tempMat(rows/4, cols, CV_8UC2);
        cv::merge(uvMats, 2, tempMat);

        memcpy(inBuffer+rows*cols, tempMat.data, rows*cols*1/2);

        //***YUV420SP FIX END

        //cv::cvtColorTwoPlane(mat, colorImg, cv::COLOR_RGBA2YUV_I420);
        // All video codecs support flexible YUV 4:2:0 buffers since Build.VERSION_CODES.LOLLIPOP_MR1.
        /*
            if(mat.channels() == 3){
                cv::cvtColor(mat, colorImg, CV_BGR2YUV_I420);
                }
            else{
                cv::cvtColor(mat, colorImg, CV_GRAY2BGR);
                cv::cvtColor(colorImg, colorImg, CV_BGR2YUV_I420);
            }
            */
        //    colorImg = mat;

        // here we actually copy the data.
        //memcpy(inBuffer, colorImg.data, out_size);

        /**
              * Send the specified buffer to the codec for processing.
              */
        //int64_t presentationTimeNs = timestamp;
        int64_t presentationTimeNs = computePresentationTimeNsec();

        media_status_t status = AMediaCodec_queueInputBuffer(mEncoder, inBufferIdx, 0, out_size, presentationTimeNs, mat.empty() ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0);

        if(status == AMEDIA_OK){
            //qDebug() << "Successfully pushed frame to input buffer";
        }
        else{
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Something went wrong while pushing frame to input buffer");
            return false;
        }

        // Submit it to the encoder.  The eglSwapBuffers call will block if the input
        // is full, which would be bad if it stayed full until we dequeued an output
        // buffer (which we can't do, since we're stuck here).  So long as we fully drain
        // the encoder before supplying additional input, the system guarantees that we
        // can supply another frame without blocking.
        //qDebug() << "sending frame " << i << " to encoder";
        //AMediaCodec_flush(mEncoder);
        return true;
    }

    JNIEXPORT void JNICALL
    EndNativeCodecEncoder(){
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "End of recording called!");
        // Send the termination frame
        ssize_t inBufferIdx = AMediaCodec_dequeueInputBuffer(mEncoder, TIMEOUT_USEC);
        size_t out_size;
        uint8_t* inBuffer = AMediaCodec_getInputBuffer(mEncoder, inBufferIdx, &out_size);
        int64_t presentationTimeNs = computePresentationTimeNsec();
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Sending EOS!");
        media_status_t status = AMediaCodec_queueInputBuffer(mEncoder, inBufferIdx, 0, out_size, presentationTimeNs, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
        // send end-of-stream to encoder, and drain remaining output

        DrainNativeCodecEncoder(true);

        ReleaseNativeCodecEncoder();

        // To test the result, open the file with MediaExtractor, and get the format.  Pass
        // that into the MediaCodec decoder configuration, along with a SurfaceTexture surface,
        // and examine the output with glReadPixels.
    }
}







/**
     * Extracts all pending data from the encoder.
     * <p>
     * If endOfStream is not set, this returns when there is no more data to drain.  If it
     * is set, we send EOS to the encoder, and then iterate until we see EOS on the output.
     * Calling this with endOfStream set should be done once, right before stopping the muxer.
     */

/**
 * Releases encoder resources.  May be called after partial / failed initialization.
 */


/**
         * Generates the presentation time for frame N, in nanoseconds.
         */

