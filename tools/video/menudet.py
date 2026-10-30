from PIL import Image
def is_menu(path):
    """the u6e title + main menu on a 640x480 Falcon capture: blue title, black gap, grey panel"""
    im = Image.open(path).convert('RGB').resize((64, 48)); px = im.load()
    left_black = all(sum(px[x, y]) < 30 for x in range(0, 6) for y in range(18, 48))
    gap_black = all(sum(px[x, y]) < 30 for x in range(64) for y in (17, 18, 19))
    blue = sum(1 for x in range(64) for y in range(6, 14) if px[x, y][2] > px[x, y][0] + 20)
    panel = sum(1 for x in range(14, 50) for y in range(21, 44) if sum(px[x, y]) > 300)
    return left_black and gap_black and blue > 30 and panel > 500
if __name__ == '__main__':
    import sys
    for p in sys.argv[1:]: print(p, is_menu(p))
