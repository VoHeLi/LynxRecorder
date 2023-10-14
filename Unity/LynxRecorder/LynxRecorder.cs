/**
 * @file LynxRecorder.cs
 * 
 * @author VoHeLi
 * 
 * @brief Unity Plugin for Lynx to record video with passthrough, AR and VR.
 * Forked from ImageProcessARVRResized.cs in Lynx Unity Plugin made by Geoffrey Marhuenda.
 */

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
using UnityEngine;
using UnityEngine.Rendering;
using static Lynx.LynxCaptureLibraryInterface;


namespace Lynx
{
    public class TimedData
    {
        public float time;
        public byte[] data;

        public TimedData(float time, byte[] data)
        {
            this.time = time;
            this.data = data;
        }
    }

    public class ExchangeCycle
    {
        private Queue<TimedData> dataQueue = new Queue<TimedData>();
        private Queue<TimedData> freeQueue = new Queue<TimedData>();

        public ExchangeCycle(int size, int count)
        {
            for (int i = 0; i < count; i++)
            {
                TimedData data = new TimedData(0.0f, new byte[size]);

                freeQueue.Enqueue(data);
            }
        }

        public TimedData GetFree()
        {
            if (freeQueue.Count > 0)
            {
                return freeQueue.Dequeue();
            }
            else
            {
                return null;
            }
        }

        public void AddData(TimedData data)
        {
            dataQueue.Enqueue(data);
        }

        public TimedData GetData()
        {
            if (dataQueue.Count > 0)
            {
                return dataQueue.Dequeue();
            }
            else
            {
                return null;
            }
        }

        public void AddFree(TimedData data)
        {
            freeQueue.Enqueue(data);
        }

        public bool IsEmpty()
        {
            return dataQueue.Count == 0;
        }
    }

    public class LynxRecorder : MonoBehaviour
    {
        #region INSPECTOR
        [Tooltip("FPS for video Recording")]
        [Range(1, 30)]
        [SerializeField] int fps = 30;


        [Tooltip("The video you want to record")]
        [SerializeField] private CaptureType captureType = CaptureType.AR;

        [Tooltip("If you want to specify a camera to be use for VR image")]
        [SerializeField] private Camera recordingCamera;

        [SerializeField] private bool pressTopRightButtonToRecord = false;
        #endregion

        private enum CaptureType
        {
            AR,
            VR,
            PassthroughOnly
        }

        private const UInt32 PASSTHROUGH_WIDTH = 1536; // Passthrough WIDTH
        private const UInt32 PASSTHROUGH_HEIGHT = 1404; // Passthrough HEIGHT

        private const UInt32 NEW_WIDTH = 1024; // Resized Width
        private const UInt32 NEW_HEIGHT = 1024; // Resized Heights

        private Thread composerThread;
        private Thread videoWriterThread;
        private Thread passthroughResizeThread;

        private ExchangeCycle passthroughRGBAEC;
        private ExchangeCycle passthroughResizedEC;
        private ExchangeCycle vrRGBAEC;
        private ExchangeCycle outputEC;

        private bool capturing = false;
        private float lastRecordedFrameTime = 0;

        private void StartVideoCapture()
        {
            LynxCaptureAPI.onRGBFrames += OnPassthroughFrameReceived;

            // Start video capture at given FPS
            if (!LynxCaptureAPI.StartCapture(LynxCaptureAPI.ESensorType.RGB, fps))
                Debug.LogError("Failed to start camera");

            if (SystemInfo.supportsAsyncGPUReadback)
                Debug.Log("Support async GPU readback");
            else
                Debug.LogError("Does not support GPU readback");
        }

        private bool recording = false;

        private void Awake()
        {
            if(recordingCamera == null)
            {
                recordingCamera = Camera.main;
            }
        }

        private void OnPassthroughFrameReceived(LynxFrameInfo frameInfo)
        {
            TimedData rgbaBytes = null;
            
            while(rgbaBytes == null)
            {
                rgbaBytes = passthroughRGBAEC.GetFree();

                if (!recording) return;
                if ((rgbaBytes == null)) Thread.Sleep(1);
            }
            
            LynxOpenCV.YUV2RGBA(frameInfo.leftEyeBuffer, frameInfo.width, frameInfo.height, rgbaBytes.data);
            rgbaBytes.time = Time.time;

            passthroughRGBAEC.AddData(rgbaBytes);
        }

        private void PassthroughResizeThread()
        {
            while (recording)
            {
                TimedData rgbaBytes = null;
                TimedData resizedRGBABytes = null;

                while (rgbaBytes == null)
                {
                    rgbaBytes = passthroughRGBAEC.GetData();
                    if (!recording) return;
                    if ((rgbaBytes == null)) Thread.Sleep(1);
                }

                while (resizedRGBABytes == null)
                {
                    resizedRGBABytes = passthroughResizedEC.GetFree();
                    if (!recording) return;
                    if ((resizedRGBABytes == null)) Thread.Sleep(1);
                }

                LynxOpenCV.ResizeFrame((int)PASSTHROUGH_WIDTH, (int)PASSTHROUGH_HEIGHT, 4, (int)NEW_WIDTH, (int)NEW_HEIGHT, rgbaBytes.data, resizedRGBABytes.data);

                resizedRGBABytes.time = rgbaBytes.time;

                passthroughResizedEC.AddData(resizedRGBABytes);
                passthroughRGBAEC.AddFree(rgbaBytes);
            }
        }

        private void ComposeThread()
        {
            while (recording)
            {
                TimedData outputBytes = null;
                while (outputBytes == null)
                {
                    outputBytes = outputEC.GetFree();
                    if (!recording) return;
                    if ((outputBytes == null)) Thread.Sleep(1);
                }

                switch (captureType)
                {
                    case CaptureType.VR:
                        TimedData vrBytes = null;

                        while (vrBytes == null)
                        {
                            vrBytes = vrRGBAEC.GetData();
                            if (!recording) return;
                            if ((vrBytes == null)) Thread.Sleep(1);
                        }

                        //We just put the vrImage in the output and take back unused data to the free queue of input

                        outputEC.AddData(vrBytes);
                        vrRGBAEC.AddFree(outputBytes);

                        break;
                    case CaptureType.PassthroughOnly:
                        TimedData resizedBytes = null;

                        while (resizedBytes == null)
                        {
                            resizedBytes = passthroughResizedEC.GetData();
                            if (!recording) return;
                            if ((resizedBytes == null)) Thread.Sleep(1);
                        }

                        //We just put the passthroughImage in the output and take back unused data to the free queue of input

                        outputEC.AddData(resizedBytes);
                        passthroughResizedEC.AddFree(outputBytes);

                        break;
                    case CaptureType.AR:
                        TimedData resizedBytesAR = null;
                        TimedData vrBytesAR = null;


                        while (vrBytesAR == null)
                        {
                            vrBytesAR = vrRGBAEC.GetData();
                            if (!recording) return;
                            if ((vrBytesAR == null)) Thread.Sleep(1);
                        }


                        while (resizedBytesAR == null)
                        {
                            resizedBytesAR = passthroughResizedEC.GetData();
                            if (!recording) return;
                            if (resizedBytesAR != null && vrBytesAR.time > resizedBytesAR.time + 0.5f / fps)
                            {
                                passthroughResizedEC.AddFree(resizedBytesAR);
                                resizedBytesAR = null;
                            }
                            if ((resizedBytesAR == null)) Thread.Sleep(1);
                        }
                        //Compose Passthrough on VR, send back passthrough as free on queue, send vr as data on output queue, and send a free from output queue to vr queue

                        LynxOpenCV.Compose_ARVR_2_RGBA_from_RGBA_AR(resizedBytesAR.data, (int)NEW_WIDTH, (int)NEW_HEIGHT, vrBytesAR.data);


                        outputEC.AddData(vrBytesAR);
                        passthroughResizedEC.AddFree(resizedBytesAR);
                        vrRGBAEC.AddFree(outputBytes);
                        break;
                }

            }
        }

        private void VideoWriteThread()
        {
            while (recording)
            {
                TimedData outputBytes = null;
                while (outputBytes == null)
                {
                    outputBytes = outputEC.GetData();
                    if (!recording)return;
                    if ((outputBytes == null)) Thread.Sleep(1);
                }

                //Block data to be sure that the data is not modified during the encoding
                GCHandle pinnedArray = GCHandle.Alloc(outputBytes.data, GCHandleType.Pinned);
                IntPtr pointer = pinnedArray.AddrOfPinnedObject();

                WriteNativeCodecEncoder((int)NEW_HEIGHT, (int)NEW_WIDTH, pointer);

                pinnedArray.Free();
                //Let the data be managed by .NET again

                outputEC.AddFree(outputBytes);
            }
        }

        private void Update()
        {
            if (pressTopRightButtonToRecord)
            {
                if (Input.GetKeyUp(KeyCode.Joystick1Button0))
                {
                    if (!recording)
                    {
                        StartRecord();
                    }
                    else
                    {
                        EndRecord();
                    }
                }
            }
        }


        //VR RGBA Method
        private void LateUpdate()
        {

            if (!recording || captureType == CaptureType.PassthroughOnly) return;

            if(Time.time - lastRecordedFrameTime < 1.0f / fps)
            {
                return;
            }

            lastRecordedFrameTime = Time.time;

            if (SystemInfo.supportsAsyncGPUReadback)
            {
                RenderTexture rt = RenderTexture.GetTemporary((int)NEW_WIDTH, (int)NEW_HEIGHT, 32);
                recordingCamera.targetTexture = rt;
                recordingCamera.Render();
                AsyncGPUReadback.Request(rt, 0, TextureFormat.RGBA32, (AsyncGPUReadbackRequest request) =>
                {
                    while (vrRGBAEC.GetFree() == null) {
                        if (!recording) return;
                        Thread.Sleep(1); 
                    }

                    byte[] rgbaBytes = request.GetData<byte>().ToArray();

                    vrRGBAEC.AddData(new TimedData(Time.time, rgbaBytes));
                });

                recordingCamera.targetTexture = null;
                RenderTexture.ReleaseTemporary(rt);
            }
        }

        public void StartRecord()
        {
            recording = true;

            //Init passthrough if needed
            if (captureType != CaptureType.VR)
            {
                if (!capturing)
                {
                    StartVideoCapture();
                    capturing = true;
                }
                
                passthroughRGBAEC = new ExchangeCycle((int)PASSTHROUGH_WIDTH * (int)PASSTHROUGH_HEIGHT * 4, 3);
                passthroughResizedEC = new ExchangeCycle((int)NEW_WIDTH * (int)NEW_HEIGHT * 4, 3);
            }

            //Init vr if needed
            if(captureType != CaptureType.PassthroughOnly)
            {
                vrRGBAEC = new ExchangeCycle((int)NEW_WIDTH * (int)NEW_HEIGHT * 4, 6);
                lastRecordedFrameTime = Time.time - 2*1.0f/fps;
            }

            //Init output
            outputEC = new ExchangeCycle((int)NEW_WIDTH * (int)NEW_HEIGHT * 4, 4);

            string filename = Application.persistentDataPath + "/video_"+ DateTime.Now.ToString("yyyy-MM-dd_hh-mm-ss") + ".mp4";
            InitNativeCodecEncoder(filename, fps, (int)NEW_HEIGHT, (int)NEW_WIDTH);
            
            PrepareNativeCodecEncoder();

            //Init Threads
            if (captureType != CaptureType.VR)
            {
                passthroughResizeThread = new Thread(PassthroughResizeThread);
                passthroughResizeThread.Start();
            }

            composerThread = new Thread(ComposeThread);
            composerThread.Start();

            videoWriterThread = new Thread(VideoWriteThread);
            videoWriterThread.Start();
        }

        public void EndRecord()
        {
            recording = false;
            
            if (captureType != CaptureType.VR)
            {
                passthroughResizeThread.Join();
            }

            composerThread.Join();
            videoWriterThread.Join();

            EndNativeCodecEncoder();
        }

        private void DrainEncoderThread()
        {
            while (recording)
            {
                DrainNativeCodecEncoder(false);
                Thread.Sleep(1);
            }
        }

        private void OnApplicationQuit()
        {
            if (!capturing) return;

            LynxCaptureAPI.StopAllCameras();
            if (recording) EndRecord();

            capturing = false;
        }

        private void OnApplicationPause(bool pause)
        {
            if (!capturing) return;

            if (pause)
            {
                LynxCaptureAPI.StopAllCameras();
                if (recording) EndRecord();
                capturing = false;
            }

        }

        private void OnApplicationFocus(bool focus)
        {
            if (!capturing) return;

            if (!focus)
            {
                LynxCaptureAPI.StopAllCameras();
                if (recording) EndRecord();
                capturing = false;
            }
        }


        [DllImport("libcvlinker", CharSet = CharSet.Ansi, CallingConvention = CallingConvention.Cdecl)]
        private static extern void InitNativeCodecEncoder([MarshalAs(UnmanagedType.LPStr)] string filename, int fps, int rows, int cols);

        [DllImport("libcvlinker")]
        private static extern void PrepareNativeCodecEncoder();

        [DllImport("libcvlinker")]
        private static extern void DrainNativeCodecEncoder(bool endOfStream);

        [DllImport("libcvlinker")]
        private static extern bool WriteNativeCodecEncoder(int rows, int cols, IntPtr data);

        [DllImport("libcvlinker")]
        private static extern void EndNativeCodecEncoder();
    }

}