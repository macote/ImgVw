#include <libheif/heif.h>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
struct ContextDeleter
{
    void operator()(heif_context* context) const
    {
        heif_context_free(context);
    }
};

struct HandleDeleter
{
    void operator()(heif_image_handle* handle) const
    {
        heif_image_handle_release(handle);
    }
};

struct ImageDeleter
{
    void operator()(heif_image* image) const
    {
        heif_image_release(image);
    }
};

struct Initialization
{
    ~Initialization()
    {
        heif_deinit();
    }
};

bool Check(const char* operation, const heif_error& error)
{
    if (error.code == heif_error_Ok)
    {
        return true;
    }

    std::cerr << operation << ": " << error.code << "/" << error.subcode << ": " << error.message << '\n';
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: libheif-decode <image.heic>\n";
        return 2;
    }

    std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
    if (!stream)
    {
        std::cerr << "Could not open input file.\n";
        return 2;
    }

    const auto size = stream.tellg();
    if (size <= 0)
    {
        std::cerr << "Input file is empty.\n";
        return 2;
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
    {
        std::cerr << "Could not read input file.\n";
        return 2;
    }

    if (!Check("heif_init", heif_init(nullptr)))
    {
        return 1;
    }
    const Initialization initialization;

    std::unique_ptr<heif_context, ContextDeleter> context(heif_context_alloc());
    if (!context ||
        !Check("read", heif_context_read_from_memory_without_copy(context.get(), data.data(), data.size(), nullptr)))
    {
        return 1;
    }

    heif_image_handle* raw_handle{};
    if (!Check("primary image", heif_context_get_primary_image_handle(context.get(), &raw_handle)))
    {
        return 1;
    }
    std::unique_ptr<heif_image_handle, HandleDeleter> handle(raw_handle);

    heif_image* raw_image{};
    if (!Check("decode",
               heif_decode_image(handle.get(), &raw_image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, nullptr)))
    {
        return 1;
    }
    std::unique_ptr<heif_image, ImageDeleter> image(raw_image);

    std::size_t stride{};
    const auto* pixels = heif_image_get_plane_readonly2(image.get(), heif_channel_interleaved, &stride);
    if (pixels == nullptr || stride == 0)
    {
        std::cerr << "Decoded image has no interleaved pixel plane.\n";
        return 1;
    }

    std::cout << heif_image_handle_get_width(handle.get()) << 'x' << heif_image_handle_get_height(handle.get())
              << ", stride " << stride << '\n';
    return 0;
}
