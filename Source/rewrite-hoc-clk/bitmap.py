from PIL import Image
import argparse
import os

def image_to_rgba8888_array(image_path, output_path):
    # Open and convert to RGBA
    img = Image.open(image_path).convert('RGBA')
    width, height = img.size

    # Get pixel data
    pixels = img.tobytes()

    # Write as C header file
    with open(output_path, 'w') as f:
        f.write('// This is a automatically generated file, do not edit manually.\n')
        f.write(f'// {os.path.basename(image_path)} - {width}x{height}\n')
        f.write(f'const unsigned int IMG_WIDTH = {width};\n')
        f.write(f'const unsigned int IMG_HEIGHT = {height};\n')
        f.write('const unsigned char IMG_DATA[] = {\n    ')

        for i, byte in enumerate(pixels):
            f.write(f'0x{byte:02X}')
            if i < len(pixels) - 1:
                f.write(', ')
                if (i + 1) % 12 == 0:
                    f.write('\n    ')

        f.write('\n};\n')

    print(f'Converted: {width}x{height} -> {len(pixels)} bytes')
    print(f'Output: {output_path}')

def main():
    parser = argparse.ArgumentParser(
        description='PNG to RGB8888 script'
    )
    parser.add_argument('input', help='Input image file (e.g. cat.png)')
    parser.add_argument(
        '-o', '--output',
        help='Output header file (default: <input>.h)'
    )

    args = parser.parse_args()

    output_path = args.output
    if not output_path:
        base, _ = os.path.splitext(args.input)
        output_path = base + '.h'

    image_to_rgba8888_array(args.input, output_path)

if __name__ == '__main__':
    main()
