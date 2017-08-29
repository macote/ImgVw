#include "turbojpeg_ImgVw.h"

// Modified TurboJPEG decompress code
// Based on libjpeg-turbo 1.5.2 (https://github.com/libjpeg-turbo/libjpeg-turbo/commit/e5c1613ccdfeffcd060fd94248b7c8ac7c0cfb0f)

namespace turbojpeg
{

    static char errStr[JMSG_LENGTH_MAX] = "No error";
    static const int pixelsize[TJ_NUMSAMP] = { 3, 3, 3, 1, 3, 3 };

    enum { COMPRESS = 1, DECOMPRESS = 2 };

#define NUMSF 16
    static const tjscalingfactor sf[NUMSF] = {
        { 2, 1 },
        { 15, 8 },
        { 7, 4 },
        { 13, 8 },
        { 3, 2 },
        { 11, 8 },
        { 5, 4 },
        { 9, 8 },
        { 1, 1 },
        { 7, 8 },
        { 3, 4 },
        { 5, 8 },
        { 1, 2 },
        { 3, 8 },
        { 1, 4 },
        { 1, 8 }
    };

#define _throw(m) {snprintf(errStr, JMSG_LENGTH_MAX, "%s", m);  \
    retval=-1;  goto bailout;}
#define getinstance(handle) tjinstance * inst=(tjinstance *)handle;  \
	j_compress_ptr cinfo=NULL;  j_decompress_ptr dinfo=NULL;  \
	if(!inst) {snprintf(errStr, JMSG_LENGTH_MAX, "Invalid handle");  \
		return -1;}  \
	cinfo=&inst->cinfo;  dinfo=&inst->dinfo;  \
	inst->jerr.warning=FALSE;
#define getcinstance(handle) tjinstance * inst=(tjinstance *)handle;  \
	j_compress_ptr cinfo=NULL;  \
	if(!inst) {snprintf(errStr, JMSG_LENGTH_MAX, "Invalid handle");  \
		return -1;}  \
	cinfo=&inst->cinfo;  \
	inst->jerr.warning=FALSE;
#define getdinstance(handle) tjinstance * inst=(tjinstance *)handle;  \
	j_decompress_ptr dinfo=NULL;  \
	if(!inst) {snprintf(errStr, JMSG_LENGTH_MAX, "Invalid handle");  \
		return -1;}  \
	dinfo=&inst->dinfo;  \
	inst->jerr.warning=FALSE;

    METHODDEF(void)
        my_error_exit(j_common_ptr cinfo)
    {
        my_error_ptr myerr = (my_error_ptr)cinfo->err;
        (*cinfo->err->output_message)(cinfo);
        longjmp(myerr->setjmp_buffer, 1);
    }

    METHODDEF(void)
        my_output_message(j_common_ptr cinfo)
    {
        (*cinfo->err->format_message)(cinfo, errStr);
    }

    METHODDEF(void)
        my_emit_message(j_common_ptr cinfo, int msg_level)
    {
        my_error_ptr myerr = (my_error_ptr)cinfo->err;
        myerr->emit_message(cinfo, msg_level);
        if (msg_level < 0) myerr->warning = TRUE;
    }

    METHODDEF(int)
        getSubsamp(j_decompress_ptr dinfo)
    {
        int retval = -1, i, k;

        /* The sampling factors actually have no meaning with grayscale JPEG files,
        and in fact it's possible to generate grayscale JPEGs with sampling
        factors > 1 (even though those sampling factors are ignored by the
        decompressor.)  Thus, we need to treat grayscale as a special case. */
        if (dinfo->num_components == 1 && dinfo->jpeg_color_space == JCS_GRAYSCALE)
            return TJSAMP_GRAY;

        for (i = 0; i < NUMSUBOPT; i++)
        {
            if (dinfo->num_components == pixelsize[i]
                || ((dinfo->jpeg_color_space == JCS_YCCK
                    || dinfo->jpeg_color_space == JCS_CMYK)
                    && pixelsize[i] == 3 && dinfo->num_components == 4))
            {
                if (dinfo->comp_info[0].h_samp_factor == tjMCUWidth[i] / 8
                    && dinfo->comp_info[0].v_samp_factor == tjMCUHeight[i] / 8)
                {
                    int match = 0;
                    for (k = 1; k < dinfo->num_components; k++)
                    {
                        int href = 1, vref = 1;
                        if (dinfo->jpeg_color_space == JCS_YCCK && k == 3)
                        {
                            href = tjMCUWidth[i] / 8;  vref = tjMCUHeight[i] / 8;
                        }
                        if (dinfo->comp_info[k].h_samp_factor == href
                            && dinfo->comp_info[k].v_samp_factor == vref)
                            match++;
                    }
                    if (match == dinfo->num_components - 1)
                    {
                        retval = i;  break;
                    }
                }
                /* Handle 4:2:2 and 4:4:0 images whose sampling factors are specified
                in non-standard ways. */
                if (dinfo->comp_info[0].h_samp_factor == 2 &&
                    dinfo->comp_info[0].v_samp_factor == 2 &&
                    (i == TJSAMP_422 || i == TJSAMP_440))
                {
                    int match = 0;
                    for (k = 1; k < dinfo->num_components; k++)
                    {
                        int href = tjMCUHeight[i] / 8, vref = tjMCUWidth[i] / 8;
                        if (dinfo->jpeg_color_space == JCS_YCCK && k == 3)
                        {
                            href = vref = 2;
                        }
                        if (dinfo->comp_info[k].h_samp_factor == href
                            && dinfo->comp_info[k].v_samp_factor == vref)
                            match++;
                    }
                    if (match == dinfo->num_components - 1)
                    {
                        retval = i;  break;
                    }
                }
            }
        }
        return retval;
    }

    METHODDEF(int)
        setDecompDefaults(struct jpeg_decompress_struct * dinfo,
            int pixelFormat, int flags)
    {
        int retval = 0;

        switch (pixelFormat)
        {
        case TJPF_GRAY:
            dinfo->out_color_space = JCS_GRAYSCALE;  break;
#if JCS_EXTENSIONS==1
        case TJPF_RGB:
            dinfo->out_color_space = JCS_EXT_RGB;  break;
        case TJPF_BGR:
            dinfo->out_color_space = JCS_EXT_BGR;  break;
        case TJPF_RGBX:
            dinfo->out_color_space = JCS_EXT_RGBX;  break;
        case TJPF_BGRX:
            dinfo->out_color_space = JCS_EXT_BGRX;  break;
        case TJPF_XRGB:
            dinfo->out_color_space = JCS_EXT_XRGB;  break;
        case TJPF_XBGR:
            dinfo->out_color_space = JCS_EXT_XBGR;  break;
#if JCS_ALPHA_EXTENSIONS==1
        case TJPF_RGBA:
            dinfo->out_color_space = JCS_EXT_RGBA;  break;
        case TJPF_BGRA:
            dinfo->out_color_space = JCS_EXT_BGRA;  break;
        case TJPF_ARGB:
            dinfo->out_color_space = JCS_EXT_ARGB;  break;
        case TJPF_ABGR:
            dinfo->out_color_space = JCS_EXT_ABGR;  break;
#endif
#else
        case TJPF_RGB:
        case TJPF_BGR:
        case TJPF_RGBX:
        case TJPF_BGRX:
        case TJPF_XRGB:
        case TJPF_XBGR:
        case TJPF_RGBA:
        case TJPF_BGRA:
        case TJPF_ARGB:
        case TJPF_ABGR:
            dinfo->out_color_space = JCS_RGB;  break;
#endif
        case TJPF_CMYK:
            dinfo->out_color_space = JCS_CMYK;  break;
        default:
            _throw("Unsupported pixel format");
        }

        if (flags&TJFLAG_FASTDCT) dinfo->dct_method = JDCT_FASTEST;

    bailout:
        return retval;
    }

    METHODDEF(void)
        init_mem_source(j_decompress_ptr cinfo)
    {
        /* no work necessary here */
    }

    METHODDEF(boolean)
        fill_mem_input_buffer(j_decompress_ptr cinfo)
    {
        static const JOCTET mybuffer[4] = {
            (JOCTET)0xFF, (JOCTET)JPEG_EOI, 0, 0
        };

        /* The whole JPEG data is expected to reside in the supplied memory
        * buffer, so any request for more data beyond the given buffer size
        * is treated as an error.
        */
        WARNMS(cinfo, JWRN_JPEG_EOF);

        /* Insert a fake EOI marker */

        cinfo->src->next_input_byte = mybuffer;
        cinfo->src->bytes_in_buffer = 2;

        return TRUE;
    }

    METHODDEF(void)
        skip_input_data(j_decompress_ptr cinfo, long num_bytes)
    {
        struct jpeg_source_mgr * src = cinfo->src;

        /* Just a dumb implementation for now.  Could use fseek() except
        * it doesn't work on pipes.  Not clear that being smart is worth
        * any trouble anyway --- large skips are infrequent.
        */
        if (num_bytes > 0) {
            while (num_bytes > (long)src->bytes_in_buffer) {
                num_bytes -= (long)src->bytes_in_buffer;
                (void)(*src->fill_input_buffer) (cinfo);
                /* note we assume that fill_input_buffer will never return FALSE,
                * so suspension need not be handled.
                */
            }
            src->next_input_byte += (size_t)num_bytes;
            src->bytes_in_buffer -= (size_t)num_bytes;
        }
    }

    METHODDEF(void)
        term_source(j_decompress_ptr cinfo)
    {
        /* no work necessary here */
    }

    char * GetErrorStr()
    {
        return errStr;
    }

    tjhandle InitDecompressInternal(tjinstance * inst)
    {
        static unsigned char buffer[1];

        /* This is also straight out of example.c */
        inst->dinfo.err = jpeg_std_error(&inst->jerr.pub);
        inst->jerr.pub.error_exit = my_error_exit;
        inst->jerr.pub.output_message = my_output_message;
        inst->jerr.emit_message = inst->jerr.pub.emit_message;
        inst->jerr.pub.emit_message = my_emit_message;

        if (setjmp(inst->jerr.setjmp_buffer))
        {
            /* If we get here, the JPEG code has signaled an error. */
            if (inst) free(inst);
            return NULL;
        }

        jpeg_create_decompress(&inst->dinfo);
        /* Make an initial call so it will create the source manager */
        PrepareBuffer(&inst->dinfo, buffer, 1);

        inst->init |= DECOMPRESS;
        return (tjhandle)inst;
    }

    tjhandle InitDecompress()
    {
        tjinstance * inst;
        if ((inst = (tjinstance *)malloc(sizeof(tjinstance))) == NULL)
        {
            snprintf(errStr, JMSG_LENGTH_MAX,
                "tjInitDecompress(): Memory allocation failure");
            return NULL;
        }

        MEMZERO(inst, sizeof(tjinstance));
        return InitDecompressInternal(inst);
    }

    int Destroy(tjhandle handle)
    {
        getinstance(handle);

        if (setjmp(inst->jerr.setjmp_buffer)) return -1;
        if (inst->init&COMPRESS) jpeg_destroy_compress(cinfo);
        if (inst->init&DECOMPRESS) jpeg_destroy_decompress(dinfo);
        free(inst);
        return 0;
    }

    void PrepareBuffer(j_decompress_ptr cinfo,
        const unsigned char * inbuffer, unsigned long insize)
    {
        struct jpeg_source_mgr * src;

        if (inbuffer == NULL || insize == 0)  /* Treat empty input as fatal error */
            ERREXIT(cinfo, JERR_INPUT_EMPTY);

        /* The source object is made permanent so that a series of JPEG images
        * can be read from the same buffer by calling jpeg_mem_src only before
        * the first one.
        */
        if (cinfo->src == NULL) {     /* first time for this JPEG object? */
            cinfo->src = (struct jpeg_source_mgr *)
                (*cinfo->mem->alloc_small) ((j_common_ptr)cinfo, JPOOL_PERMANENT,
                    sizeof(struct jpeg_source_mgr));
        }
        else if (cinfo->src->init_source != init_mem_source) {
            /* It is unsafe to reuse the existing source manager unless it was created
            * by this function.
            */
            ERREXIT(cinfo, JERR_BUFFER_SIZE);
        }

        src = cinfo->src;
        src->init_source = init_mem_source;
        src->fill_input_buffer = fill_mem_input_buffer;
        src->skip_input_data = skip_input_data;
        src->resync_to_restart = jpeg_resync_to_restart; /* use default method */
        src->term_source = term_source;
        src->bytes_in_buffer = (size_t)insize;
        src->next_input_byte = (const JOCTET *)inbuffer;
    }

    int DecompressHeader(tjhandle handle,
        const unsigned char * jpegBuf, unsigned long jpegSize, int * width,
        int * height, int * jpegSubsamp, int * jpegColorspace, int flags)
    {
        int retval = 0;

        getdinstance(handle);
        if ((inst->init&DECOMPRESS) == 0)
            _throw("tjDecompressHeader3(): Instance has not been initialized for decompression");

        if (jpegBuf == NULL || jpegSize <= 0 || width == NULL || height == NULL
            || jpegSubsamp == NULL || jpegColorspace == NULL)
            _throw("tjDecompressHeader3(): Invalid argument");

        if (setjmp(inst->jerr.setjmp_buffer))
        {
            /* If we get here, the JPEG code has signaled an error. */
            return -1;
        }

        PrepareBuffer(dinfo, jpegBuf, jpegSize);
        jpeg_read_header(dinfo, TRUE);

        *width = dinfo->image_width;
        *height = dinfo->image_height;
        *jpegSubsamp = getSubsamp(dinfo);
        switch (dinfo->jpeg_color_space)
        {
        case JCS_GRAYSCALE:  *jpegColorspace = TJCS_GRAY;  break;
        case JCS_RGB:        *jpegColorspace = TJCS_RGB;  break;
        case JCS_YCbCr:      *jpegColorspace = TJCS_YCbCr;  break;
        case JCS_CMYK:       *jpegColorspace = TJCS_CMYK;  break;
        case JCS_YCCK:       *jpegColorspace = TJCS_YCCK;  break;
        default:             *jpegColorspace = -1;  break;
        }

        if (!(flags&TJFLAG_NOCLEANUP)) jpeg_abort_decompress(dinfo);

        if (*jpegSubsamp < 0)
            _throw("tjDecompressHeader3(): Could not determine subsampling type for JPEG image");
        if (*jpegColorspace < 0)
            _throw("tjDecompressHeader3(): Could not determine colorspace of JPEG image");
        if (*width < 1 || *height < 1)
            _throw("tjDecompressHeader3(): Invalid data returned in header");

    bailout:
        if (inst->jerr.warning) retval = -1;
        return retval;
    }

#define DSTATE_START 200 /* after create_decompress */

    int Decompress(tjhandle handle,
        const unsigned char * jpegBuf, unsigned long jpegSize, unsigned char * dstBuf,
        int width, int pitch, int height, int pixelFormat, int flags)
    {
        int i, retval = 0; JSAMPROW * row_pointer = NULL;
        int jpegwidth, jpegheight, scaledw, scaledh;

        getdinstance(handle);
        if ((inst->init&DECOMPRESS) == 0)
            _throw("tjDecompress2(): Instance has not been initialized for decompression");

        if (jpegBuf == NULL || jpegSize <= 0 || dstBuf == NULL || width < 0 || pitch < 0
            || height < 0 || pixelFormat < 0 || pixelFormat >= TJ_NUMPF)
            _throw("tjDecompress2(): Invalid argument");

        if (setjmp(inst->jerr.setjmp_buffer))
        {
            /* If we get here, the JPEG code has signaled an error. */
            retval = -1;
            goto bailout;
        }

        PrepareBuffer(dinfo, jpegBuf, jpegSize);
        jpeg_read_header(dinfo, TRUE);
        if (setDecompDefaults(dinfo, pixelFormat, flags) == -1)
        {
            retval = -1;  goto bailout;
        }

        if (flags&TJFLAG_FASTUPSAMPLE) dinfo->do_fancy_upsampling = FALSE;

        jpegwidth = dinfo->image_width;  jpegheight = dinfo->image_height;
        if (width == 0) width = jpegwidth;
        if (height == 0) height = jpegheight;
        for (i = 0; i < NUMSF; i++)
        {
            scaledw = TJSCALED(jpegwidth, sf[i]);
            scaledh = TJSCALED(jpegheight, sf[i]);
            if (scaledw <= width && scaledh <= height)
                break;
        }
        if (i >= NUMSF)
            _throw("tjDecompress2(): Could not scale down to desired image dimensions");
        width = scaledw;  height = scaledh;
        dinfo->scale_num = sf[i].num;
        dinfo->scale_denom = sf[i].denom;

        jpeg_start_decompress(dinfo);
        if (pitch == 0) pitch = dinfo->output_width * tjPixelSize[pixelFormat];

        if ((row_pointer = (JSAMPROW *)malloc(sizeof(JSAMPROW)
            * dinfo->output_height)) == NULL)
            _throw("tjDecompress2(): Memory allocation failure");
        if (setjmp(inst->jerr.setjmp_buffer))
        {
            /* If we get here, the JPEG code has signaled an error. */
            retval = -1;
            goto bailout;
        }
        for (i = 0; i < (int)dinfo->output_height; i++)
        {
            if (flags&TJFLAG_BOTTOMUP)
                row_pointer[i] = &dstBuf[(dinfo->output_height - i - 1) * pitch];
            else row_pointer[i] = &dstBuf[i * pitch];
        }
        while (dinfo->output_scanline < dinfo->output_height)
        {
            jpeg_read_scanlines(dinfo, &row_pointer[dinfo->output_scanline],
                dinfo->output_height - dinfo->output_scanline);
        }
        jpeg_finish_decompress(dinfo);

    bailout:
        if (dinfo->global_state > DSTATE_START) jpeg_abort_decompress(dinfo);
        if (row_pointer) free(row_pointer);
        if (inst->jerr.warning) retval = -1;
        return retval;
    }

    int AbortDecompress(tjhandle handle)
    {
        int retval = 0;

        getdinstance(handle);

        jpeg_abort_decompress(dinfo);
        if (inst->jerr.warning) retval = -1;

        return retval;
    }

    tjscalingfactor* GetScalingFactors(int * numscalingfactors)
    {
        if (numscalingfactors == NULL)
        {
            snprintf(errStr, JMSG_LENGTH_MAX,
                "tjGetScalingFactors(): Invalid argument");
            return NULL;
        }

        *numscalingfactors = NUMSF;
        return (tjscalingfactor *)sf;
    }

    int SaveMarkers(tjhandle handle, int markercode)
    {
        getdinstance(handle);

        jpeg_save_markers(dinfo, markercode, 0xFFFF);

        return 0;
    }

    int SkipMarkers(tjhandle handle, int markercode)
    {
        getdinstance(handle);

        jpeg_save_markers(dinfo, markercode, 0);

        return 0;
    }

#define EXIF_OVERHEAD_LEN 6

    METHODDEF(int)
        marker_is_exif(jpeg_saved_marker_ptr marker)
    {
        return
            marker->marker == EXIF_MARKER &&
            marker->data_length >= EXIF_OVERHEAD_LEN &&
            /* verify the identifying string */
            GETJOCTET(marker->data[0]) == 0x45 &&
            GETJOCTET(marker->data[1]) == 0x78 &&
            GETJOCTET(marker->data[2]) == 0x69 &&
            GETJOCTET(marker->data[3]) == 0x66 &&
            GETJOCTET(marker->data[4]) == 0x0 &&
            GETJOCTET(marker->data[5]) == 0x0;
    }

#define ICC_OVERHEAD_LEN 14 /* size of non-profile data in APP2 */

    METHODDEF(int)
        marker_is_icc(jpeg_saved_marker_ptr marker)
    {
        return
            marker->marker == ICC_MARKER &&
            marker->data_length >= ICC_OVERHEAD_LEN &&
            /* verify the identifying string */
            GETJOCTET(marker->data[0]) == 0x49 &&
            GETJOCTET(marker->data[1]) == 0x43 &&
            GETJOCTET(marker->data[2]) == 0x43 &&
            GETJOCTET(marker->data[3]) == 0x5F &&
            GETJOCTET(marker->data[4]) == 0x50 &&
            GETJOCTET(marker->data[5]) == 0x52 &&
            GETJOCTET(marker->data[6]) == 0x4F &&
            GETJOCTET(marker->data[7]) == 0x46 &&
            GETJOCTET(marker->data[8]) == 0x49 &&
            GETJOCTET(marker->data[9]) == 0x4C &&
            GETJOCTET(marker->data[10]) == 0x45 &&
            GETJOCTET(marker->data[11]) == 0x0;
    }

#define MAX_SEQ_NO  255	/* sufficient since marker numbers are bytes */

    int ReadICCProfile(tjhandle handle, JOCTET ** iccprofile, int* iccprofilebytecount)
    {
        getdinstance(handle);

        jpeg_saved_marker_ptr marker;
        int num_markers = 0;
        int seq_no;
        JOCTET * icc_data;
        unsigned int total_length;
        char marker_present[MAX_SEQ_NO + 1];	  /* 1 if marker found */
        unsigned int data_length[MAX_SEQ_NO + 1]; /* size of profile data in marker */
        unsigned int data_offset[MAX_SEQ_NO + 1]; /* offset for data in marker */

        *iccprofile = NULL;
        *iccprofilebytecount = 0;

        /* This first pass over the saved markers discovers whether there are
        * any ICC markers and verifies the consistency of the marker numbering.
        */

        for (seq_no = 1; seq_no <= MAX_SEQ_NO; seq_no++)
            marker_present[seq_no] = 0;

        for (marker = dinfo->marker_list; marker != NULL; marker = marker->next) {
            if (marker_is_icc(marker)) {
                if (num_markers == 0)
                    num_markers = GETJOCTET(marker->data[13]);
                else if (num_markers != GETJOCTET(marker->data[13]))
                    return FALSE;		/* inconsistent num_markers fields */
                seq_no = GETJOCTET(marker->data[12]);
                if (seq_no <= 0 || seq_no > num_markers)
                    return FALSE;		/* bogus sequence number */
                if (marker_present[seq_no])
                    return FALSE;		/* duplicate sequence numbers */
                marker_present[seq_no] = 1;
                data_length[seq_no] = marker->data_length - ICC_OVERHEAD_LEN;
            }
        }

        if (num_markers == 0)
            return FALSE;

        /* Check for missing markers, count total space needed,
        * compute offset of each marker's part of the data.
        */

        total_length = 0;
        for (seq_no = 1; seq_no <= num_markers; seq_no++) {
            if (marker_present[seq_no] == 0)
                return FALSE;		/* missing sequence number */
            data_offset[seq_no] = total_length;
            total_length += data_length[seq_no];
        }

        if (total_length == 0)
            return FALSE;		/* found only empty markers? */

                                /* Allocate space for assembled data */
        icc_data = (JOCTET *)malloc(total_length * sizeof(JOCTET));
        if (icc_data == NULL)
            return FALSE;		/* oops, out of memory */

                                /* and fill it in */
        for (marker = dinfo->marker_list; marker != NULL; marker = marker->next) {
            if (marker_is_icc(marker)) {
                JOCTET FAR * src_ptr;
                JOCTET * dst_ptr;
                unsigned int length;
                seq_no = GETJOCTET(marker->data[12]);
                dst_ptr = icc_data + data_offset[seq_no];
                src_ptr = marker->data + ICC_OVERHEAD_LEN;
                length = data_length[seq_no];
                while (length--) {
                    *dst_ptr++ = *src_ptr++;
                }
            }
        }

        *iccprofile = icc_data;
        *iccprofilebytecount = total_length;

        return TRUE;
    }

    int LocateEXIFSegment(tjhandle handle, JOCTET ** exifdata)
    {
        getdinstance(handle);

        jpeg_saved_marker_ptr marker;
        *exifdata = NULL;

        for (marker = dinfo->marker_list; marker != NULL; marker = marker->next) {
            if (marker_is_exif(marker)) {
                *exifdata = marker->data;
                return marker->data_length;
            }
        }

        return 0;
    }
}
