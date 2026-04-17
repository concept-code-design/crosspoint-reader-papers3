import sys
import os

# On macOS with Homebrew, cairo lives in /opt/homebrew/lib.
# cairosvg's cffi backend searches DYLD_LIBRARY_PATH, so inject it before import.
if sys.platform == 'darwin':
    _brew_lib = '/opt/homebrew/lib'
    if os.path.isdir(_brew_lib):
        os.environ['DYLD_LIBRARY_PATH'] = (
            _brew_lib + ':' + os.environ.get('DYLD_LIBRARY_PATH', ''))

from PIL import Image
import cairosvg
import io

threshold = 128

def svg_to_png_bytes(svg_path, width, height):
    with open(svg_path, 'rb') as f:
        svg_data = f.read()
    png_bytes = cairosvg.svg2png(bytestring=svg_data, output_width=width, output_height=height)
    return png_bytes

def load_image(path, width, height):
    ext = os.path.splitext(path)[1].lower()
    if ext == '.svg':
        png_bytes = svg_to_png_bytes(path, width, height)
        img = Image.open(io.BytesIO(png_bytes)).convert('RGBA')
        # Flatten alpha onto white (cairosvg outputs transparent background)
        background = Image.new('RGBA', img.size, (255, 255, 255, 255))
        background.paste(img, mask=img.split()[3])
        img = background
    else:
        img = Image.open(path)
        img = img.convert('RGBA')
        img = img.resize((width, height), Image.LANCZOS)
        # Flatten alpha: paste on white background
        background = Image.new('RGBA', img.size, (255, 255, 255, 255))
        background.paste(img, mask=img.split()[3])
        img = background
    # Rotate 90 degrees counterclockwise
    img = img.rotate(90, expand=True)
    return img

def image_to_c_array(img, array_name):
    # Convert to grayscale, then threshold to get white=1, black=0
    # Convert to grayscale
    img = img.convert('L')
    width, height = img.size
    pixels = list(img.getdata())
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    v = pixels[y * width + x + b]
                    # 1 for white, 0 for black
                    bit = 1 if v >= threshold else 0
                    byte |= (bit << (7 - b))
            packed.append(byte)
    # Format as C array
    c = f'#pragma once\n#include <cstdint>\n\n'
    c += f'// size: {width}x{height}\n'
    c += f'static const uint8_t {array_name}[] = {{\n    '
    for i, v in enumerate(packed):
        c += f'0x{v:02X}, '
        if (i + 1) % 16 == 0:
            c += '\n    '
    c = c.rstrip(', \n') + '\n};\n'
    return c

def main():
    if len(sys.argv) < 2:
        print('Usage: python convert_icon.py input.svg [output_name] [width] [height]')
        print('  output_name defaults to the input filename stem')
        print('  width/height default to 32')
        sys.exit(1)
    input_path = sys.argv[1]
    output_name = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(os.path.basename(input_path))[0]
    width  = int(sys.argv[3]) if len(sys.argv) > 3 else 32
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 32
    array_name = output_name.capitalize() + 'Icon'
    img = load_image(input_path, width, height)
    c_array = image_to_c_array(img, array_name)

    # Always save to src/components/icons/[output_name].h relative to project root
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    output_dir = os.path.join(project_root, 'src', 'components', 'icons')
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, f'{output_name}.h')
    with open(output_path, 'w') as f:
        f.write(c_array)
    print(f'Wrote {output_path}')

if __name__ == '__main__':
    main()