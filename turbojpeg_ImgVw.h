#pragma once

// Modified TurboJPEG decompress code
// Based on libjpeg-turbo 1.5.2 (https://github.com/libjpeg-turbo/libjpeg-turbo/commit/e5c1613ccdfeffcd060fd94248b7c8ac7c0cfb0f)

#include <stdio.h>
#include <setjmp.h>
#include "3rd-party\libjpeg-turbo\jpeglib.h"
#include "3rd-party\libjpeg-turbo\jerror.h"
#include "3rd-party\libjpeg-turbo\turbojpeg.h"
#include <Windows.h>

#define EXIF_MARKER (JPEG_APP0 + 1)
#define ICC_MARKER (JPEG_APP0 + 2)
#define TJFLAG_NOCLEANUP 32768

#define MEMZERO(target,size) ZeroMemory((void *)(target), (size_t)(size))

namespace turbojpeg
{
    struct my_error_mgr
    {
        struct jpeg_error_mgr pub;
        jmp_buf setjmp_buffer;
        void(*emit_message)(j_common_ptr, int);
        boolean warning;
    };
    typedef struct my_error_mgr * my_error_ptr;

    typedef struct _tjinstance
    {
        struct jpeg_compress_struct cinfo;
        struct jpeg_decompress_struct dinfo;
        struct my_error_mgr jerr;
        int init, headerRead;
    } tjinstance;

    char * GetErrorStr();
    tjhandle InitDecompress();
    tjhandle InitDecompressInternal(tjinstance * pinstance);
    int Destroy(tjhandle handle);
    int AbortDecompress(tjhandle handle);
    int SaveMarkers(tjhandle handle, int markercode);
    int SkipMarkers(tjhandle handle, int markercode);
    void PrepareBuffer(j_decompress_ptr cinfo, const unsigned char * inbuffer, unsigned long insize);
    int DecompressHeader(tjhandle handle,
        const unsigned char * jpegBuf, unsigned long jpegSize, int * width,
        int * height, int * jpegSubsamp, int * jpegColorspace, int flags);
    int Decompress(tjhandle handle,
        const unsigned char * jpegBuf, unsigned long jpegSize, unsigned char * dstBuf,
        int width, int pitch, int height, int pixelFormat, int flags);
    tjscalingfactor * GetScalingFactors(int * numscalingfactors);
    int LocateEXIFSegment(tjhandle handle, JOCTET ** exifdata);
    int ReadICCProfile(tjhandle handle, JOCTET ** iccprofile, int * iccprofilebytecount);
}
