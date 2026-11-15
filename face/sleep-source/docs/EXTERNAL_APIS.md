# External APIs

All application-defined functions are documented in FUNCTIONS.md. Library call groups below summarize their use; authoritative declarations are in Windows SDK / C++ / ONNX Runtime headers.

| Group | Calls and purpose |
|---|---|
| COM lifecycle/ownership | CoInitializeEx, CoUninitialize, CoCreateInstance, CoTaskMemFree, Microsoft::WRL::ComPtr, HRESULT checks. Main initializes COM before codecs/capture; background neural inference does not use COM. |
| WIC | IWICImagingFactory::CreateStream/CreateDecoderFromStream/CreateFormatConverter/CreateEncoder, IWICStream::InitializeFromFilename/InitializeFromMemory, decoder GetFrame, frame GetSize, converter Initialize/CopyPixels. Decode first image to owned BGR24. |
| WIC JPEG | encoder Initialize/CreateNewFrame/Commit; IPropertyBag2::Write and VariantInit set quality; frame Initialize/SetSize/SetPixelFormat/WritePixels/Commit. CreateStreamOnHGlobal, IStream::Seek/Read/Stat provide auto-freed encoded bytes. |
| Media Foundation | MFStartup/MFShutdown, MFCreateAttributes, SetGUID/SetUINT32, MFEnumDeviceSources, IMFActivate::ActivateObject, IMFMediaSource::Shutdown, MFCreateSourceReaderFromMediaSource/FromURL. Discover/open sources and choose native format. |
| MF formats/samples | GetNativeMediaType, GetCurrentMediaType, SetCurrentMediaType, MFCreateMediaType, MFGetAttributeSize/Ratio, IMFMediaType::GetGUID/GetUINT32, IMFSourceReader::ReadSample, IMFSample::ConvertToContiguousBuffer, IMFMediaBuffer::Lock/Unlock. Decode RGB32 or normalize exposed raw layouts and convert full-range MJPEG YUY2 with stride/length checks. |
| GDI text/surfaces | CreateCompatibleDC, CreateDIBSection, CreateFontW, SelectObject, SetBkMode, SetTextColor, TextOutW, GdiFlush, DeleteObject, DeleteDC. Own/copy BGR pixels and release temporary graphics objects. |
| Win32 preview | RegisterClassW, GetModuleHandleW, LoadCursor, CreateWindowW, DefWindowProcW, DestroyWindow, PeekMessageW, TranslateMessage, DispatchMessageW, GetClientRect, GetDC, StretchDIBits, ReleaseDC. Display frame and process controls on main thread. |
| Paths/runtime | GetModuleFileNameW/GetFullPathNameW, MultiByteToWideChar/WideCharToMultiByte, LoadLibraryW/GetProcAddress/FreeLibrary. Resolve assets and application-local neural DLL; model paths use UTF-16. MessageBeep is optional. _mkdir creates output leaf. Legacy MAX_PATH constraints remain in executable-root fallback. |
| Neural C API | OrtGetApiBase/GetApi; environment Create/Release; GetErrorMessage/ReleaseStatus; session option Create/Release, thread/optimization settings, optional DirectML append plus sequential/no-memory-pattern settings; session Create/Release; allocator/name enumeration; CPU memory Create/Release; tensor Create/Run/type-shape/data access/Release. Native statuses become exceptions; outputs are validated and deeply copied. |
| C++ ownership/concurrency | vector/deque/string/unique_ptr, thread/join, mutex/lock_guard/unique_lock, condition_variable wait/notify, atomics. Image/Tensor vectors own storage; latest-frame worker bounds pending work. |
| C++ numerical/timing | min/max/sort, abs/hypot/sin/cos/atan2/sqrt/exp/isfinite, numeric_limits, chrono::steady_clock and sleep_until. Native interpolation, geometry, temporal integration, benchmarking and pacing. |
| C++ files/text | fstream/ofstream, read/write/seekg/seekp/tellg/tellp/flush/close, string and stream formatting/parsing, stoi/stoul/stod, exception::what. AVI parsing bounds chunks and limits nesting/size; runtime file failures propagate. |
| CMake | out-of-source path checks, MSVC/x64 guards, C++14/native Windows libraries, target definitions, POST_BUILD neural DLL copying, CTest, compiler configuration evidence. No IDE projects are required. |
| PowerShell validation | JSON conversion, Get-FileHash, native process invocation, PE BinaryReader, directory scans, CTest/dumpbin output checks. Scripts fail if model/runtime integrity, compiler architecture, imports, functional checks or paced performance regress. |

No third-party image-processing or camera library is linked. Windows codec availability and camera driver behavior are platform dependencies. Media Foundation calls are synchronous: a blocked driver cannot be forcibly timed out here. Physical IR accuracy is not implied by successful API calls.
