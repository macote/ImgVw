#include "JpegFixture.h"

#include <cstdio>
#include <jpeglib.h>

#include <cstdlib>

std::vector<unsigned char> CreateJpeg(bool cmyk, bool include_metadata)
{
    jpeg_compress_struct compressor{};
    jpeg_error_mgr error_manager{};
    compressor.err = jpeg_std_error(&error_manager);
    jpeg_create_compress(&compressor);

    unsigned char* compressed_data{};
    unsigned long compressed_size{};
    jpeg_mem_dest(&compressor, &compressed_data, &compressed_size);

    compressor.image_width = 4;
    compressor.image_height = 2;
    compressor.input_components = cmyk ? 4 : 3;
    compressor.in_color_space = cmyk ? JCS_CMYK : JCS_RGB;
    jpeg_set_defaults(&compressor);
    jpeg_set_quality(&compressor, 90, TRUE);
    jpeg_start_compress(&compressor, TRUE);

    if (include_metadata)
    {
        const unsigned char exif[] = {'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0};
        const unsigned char icc[] = {1, 2, 3, 4, 5, 6};
        jpeg_write_marker(&compressor, JPEG_APP0 + 1, exif, sizeof(exif));
        jpeg_write_icc_profile(&compressor, icc, sizeof(icc));
    }

    std::vector<unsigned char> pixels(static_cast<std::size_t>(compressor.image_width) * compressor.input_components);
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<unsigned char>((index * 31) & 0xFF);
    }

    while (compressor.next_scanline < compressor.image_height)
    {
        auto row = pixels.data();
        jpeg_write_scanlines(&compressor, &row, 1);
    }

    jpeg_finish_compress(&compressor);
    std::vector<unsigned char> result(compressed_data, compressed_data + compressed_size);
    jpeg_destroy_compress(&compressor);
    std::free(compressed_data);
    return result;
}
