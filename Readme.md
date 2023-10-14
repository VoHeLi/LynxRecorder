# Lynx Recorder Unity Plugin

This is a Unity plugin for Lynx that allows you to record video with passthrough, AR, and VR support. It is a community-driven project and is not officially endorsed or supported by Lynx. The Lynx Recorder plugin enables you to capture and record video frames with various modes and options.

## Table of Contents
- [Introduction](#lynx-recorder-unity-plugin)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [Configuration](#configuration)
- [Known Issues](#known-issues)
- [License](#license)

## Features
- Adjustable frame rate for video recording.
- Real-time video frame composition for VR, AR or Passthrough Only.
- Multithreading Architecture which doesn't slow the usage of your app.

## Usage
1. Open a project which can build on the Lynx.
2. Import Unity3D Previews SDK in your project (Samples should not be needed), and __DISABLE DEVELOPMENT BUILD__.
3. Make sure you have the camera permissi enabled. You can (re)install your .apk and give it all the permissions with `adb install -g yourapp.apk` and test with Lynx Samples if the app can access cameras.
4. Import the UnityPackage from this repo or the files in the Unity folder. (Be careful that the .so files are located in Plugins/Android/)
5. Attach the `LynxRecorder` script to a GameObject in your Unity scene.
6. Configure the parameters in the Unity Inspector (see the [Configuration](#configuration) section below for details).
7. Enable PressTopRightButtonToRecord and press the top button in your app, __OR__ call StartRecord/EndRecord from one of your scripts, when you need to.
8. The recorded video will be saved as an MP4 file in the persistent data path of your Unity project. (Android/data/[app_name]/files)
   You can access your Lynx files by using the command : `adb shell svc usb setFunctions mtp` after plugging the USB.

## Configuration
### Inspector Parameters
- **FPS:** Set the frame rate for video recording, ranging from 1 to 30 frames per second. (Avoid going >20fps)
- **Capture Type:** Choose from three capture types: AR, VR, or PassthroughOnly.
- **Recording Camera:** Optionally specify a camera to be used for VR image capture.
- **Press Top Right Button to Record:** If enabled, you can start and stop recording by pressing the specified button.

## Known Issues
- Time distorsion if the fps are too high.
- Record -> Sleep -> Record makes the app crashes.

## License
This Lynx Recorder Unity Plugin is released under the [CC0 (Creative Commons Zero) License](LICENSE.md). You are free to use, modify, and distribute the code without any restrictions.

**Please note: This plugin is not officially endorsed or supported by Lynx. Use it at your own discretion.**

For specific licensing information and details on how to contribute or report issues, please refer to the [LICENSE file](LICENSE.md) and the project's repository.

By contributing to this project, you agree to release your contributions under the [CC0 (Creative Commons Zero) License](LICENSE.md).

For more information about the CC0 license, visit [Creative Commons CC0](https://creativecommons.org/publicdomain/zero/1.0/).
