from PIL import Image 

image = Image.open("font.png")

for c in range(10):
    array = []
    for x in range(5):
        b = 0
        for y in range(8):
            b *= 2
            if image.getpixel((c*6+x, y))[0] > 127:
                b += 1
        array.append(b)
    s = ', '.join(['0x{0:02X}'.format(b) for b in array])
    print('\t{' + s + '}, ')
    
