import time
import random
import os
import sys
import termios
import tty
import atexit

TARGET_FPS = 60
FRAME_TIME = 1 / TARGET_FPS
MOVE_TIMEOUT = 1
NEW_PIECE_TIMEOUT = 0.100
SPEEDUP_TIMEOUT = FRAME_TIME
SCORE_EFFECT_TIMEOUT = NEW_PIECE_TIMEOUT

PIECES = [
    # I
    [[[0, 0, 0, 0],
      [1, 1, 1, 1],
      [0, 0, 0, 0],
      [0, 0, 0, 0]],

     [[0, 0, 1, 0],
      [0, 0, 1, 0],
      [0, 0, 1, 0],
      [0, 0, 1, 0]],

     [[0, 0, 0, 0],
      [0, 0, 0, 0],
      [1, 1, 1, 1],
      [0, 0, 0, 0]],

     [[0, 1, 0, 0],
      [0, 1, 0, 0],
      [0, 1, 0, 0],
      [0, 1, 0, 0]]],

    # O
    [[[0, 0, 0, 0],
      [0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]],

     [[0, 0, 0, 0],
      [0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]],

     [[0, 0, 0, 0],
      [0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]],

     [[0, 0, 0, 0],
      [0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]]],

    # L
    [[[1, 0, 0],
      [1, 1, 1],
      [0, 0, 0]],

     [[0, 1, 1],
      [0, 1, 0],
      [0, 1, 0]],

     [[0, 0, 0],
      [1, 1, 1],
      [0, 0, 1]],

     [[0, 1, 0],
      [0, 1, 0],
      [1, 1, 0]]],

     # J
    [[[0, 0, 1],
      [1, 1, 1],
      [0, 0, 0]],

     [[0, 1, 0],
      [0, 1, 0],
      [0, 1, 1]],

     [[0, 0, 0],
      [1, 1, 1],
      [1, 0, 0]],

     [[1, 1, 0],
      [0, 1, 0],
      [0, 1, 0]]],

     # S
    [[[0, 1, 1],
      [1, 1, 0],
      [0, 0, 0]],

     [[0, 1, 0],
      [0, 1, 1],
      [0, 0, 1]],

     [[0, 0, 0],
      [0, 1, 1],
      [1, 1, 0]],

     [[1, 0, 0],
      [1, 1, 0],
      [0, 1, 0]]],

    # Z
    [[[1, 1, 0],
      [0, 1, 1],
      [0, 0, 0]],

     [[0, 0, 1],
      [0, 1, 1],
      [0, 1, 0]],

     [[0, 0, 0],
      [1, 1, 0],
      [0, 1, 1]],

     [[0, 1, 0],
      [1, 1, 0],
      [1, 0, 0]]],

    # T
    [[[0, 1, 0],
      [1, 1, 1],
      [0, 0, 0]],

     [[0, 1, 0],
      [0, 1, 1],
      [0, 1, 0]],

     [[0, 0, 0],
      [1, 1, 1],
      [0, 1, 0]],

     [[0, 1, 0],
      [1, 1, 0],
      [0, 1, 0]]],
]

MINROWS = 4
MAXROWS = 20
MINCOLS = 4
MAXCOLS = 10
BORDER_COLOR = 47

def clear():
    print('\x1b[2J', end='')

def move_cursor(row, col):
    print(f'\x1b[{row};{col}f', end='')

def print_border(length, color):
    print(f'\x1b[{color}m' + (' ' * length) + '\x1b[0m', end='')

def flush():
    print(end='', flush=True)

def output(text):
    print(text, end='')

def output_color(text, fg=39, bg=49):
    print(f'\x1b[{fg};{bg}m' + text + '\x1b[0m', end='')

class Tetris:
    def __init__(self, screen_rect):
        top, left, rows, cols = screen_rect
        self.rows = rows - 2
        if self.rows < MINROWS:
            raise ValueError('not enough terminal rows')
        self.rows = min(self.rows, MAXROWS)
        self.total_rows = self.rows + 2 # playing field + borders

        self.cols = cols - 2
        if self.cols < MINCOLS:
            raise ValueError('not enough terminal columns')
        self.cols = min(self.cols, MAXCOLS)
        self.total_cols = self.cols + 2 # playing field + borders

        self.top = top + rows // 2 - self.total_rows // 2
        self.left = left + cols // 2 - self.total_cols // 2
        self.grid = [[None] * self.cols for _ in range(self.rows)]

        self.new_piece()
        self.state = self.process_move
        self.time_to_move = MOVE_TIMEOUT

    def new_piece(self):
        sprites = random.choice(PIECES)
        width = len(sprites[0][0])
        height = len(sprites[0])
        self.piece =  {
            'color': random.choice(range(41, 47)),
            'sprites': sprites,
            'sprite_idx': 0,
            'x': self.cols // 2 - width // 2,
            'y': 0,
            'width': width,
            'height': height,
        }
        self.speedup_time = 0

    def check_collision(self):
        sprite = self.piece['sprites'][self.piece['sprite_idx']]
        x0, y0 = self.piece['x'], self.piece['y']
        for r, cols in enumerate(sprite):
            for c, filled in enumerate(cols):
                if not filled:
                    continue
                x = x0 + c
                y = y0 + r
                if x < 0 or x >= self.cols or y < 0 or y >= self.rows:
                    return True
                if self.grid[y][x]:
                    return True
        return False

    def consume_piece(self):
        sprite = self.piece['sprites'][self.piece['sprite_idx']]
        x0, y0 = self.piece['x'], self.piece['y']
        for r, cols in enumerate(sprite):
            for c, filled in enumerate(cols):
                if not filled:
                    continue
                x = x0 + c
                y = y0 + r
                assert self.grid[y][x] is None
                self.grid[y][x] = self.piece['color']
        self.piece = None

    def make_new_piece(self):
        self.new_piece()
        if self.check_collision():
            return False

        self.state = self.process_move
        self.time_to_move = MOVE_TIMEOUT
        return True

    def process_wait_for_new_piece(self, dt, keys):
        self.time_to_new_piece -= dt
        if self.time_to_new_piece > 0:
            return True
        return self.make_new_piece()

    def check_filled_rows(self, y0, height):
        self.filled = []
        for y in range(y0, min(y0 + height, self.rows)):
            filled = True
            for block in self.grid[y]:
                if block is None:
                    filled = False
                    break
            if filled:
                self.filled.append(y)
        return len(self.filled) > 0

    def process_score_effect(self, dt, keys):
        self.score_effect_time -= dt
        if self.score_effect_time > 0:
            return True

        assert len(self.filled) > 0
        for y in reversed(self.filled):
            self.grid.pop(y)
        for _ in self.filled:
            self.grid.insert(0, [None] * self.cols)
        self.filled = []
        return self.make_new_piece()

    def process_move(self, dt, keys):
        if self.speedup_time > 0:
            self.speedup_time -= dt
            dt *= 40

        self.time_to_move -= dt
        if self.time_to_move <= 0:
            self.time_to_move += MOVE_TIMEOUT
            self.piece['y'] += 1 # descend
            if self.check_collision():
                self.piece['y'] -= 1 # collision, revert
                y0, height = self.piece['y'], self.piece['height']
                self.consume_piece()
                print(y0, height, len(self.grid))
                if self.check_filled_rows(y0, height):
                    self.state = self.process_score_effect
                    self.score_effect_time = SCORE_EFFECT_TIMEOUT
                else:
                    self.state = self.process_wait_for_new_piece
                    self.time_to_new_piece = NEW_PIECE_TIMEOUT
                return True

        for key in keys:
            if key == 'left':
                self.piece['x'] -= 1
                if self.check_collision():
                    self.piece['x'] += 1
            elif key == 'right':
                self.piece['x'] += 1
                if self.check_collision():
                    self.piece['x'] -= 1
            elif key == 'up':
                orig = self.piece['sprite_idx']
                self.piece['sprite_idx'] += 1
                self.piece['sprite_idx'] %= len(self.piece['sprites'])
                if self.check_collision():
                    self.piece['sprite_idx'] = orig
            elif key == 'down':
                self.speedup_time = SPEEDUP_TIMEOUT
        return True

    def update(self, dt, keys):
        return self.state(dt, keys)

    def draw(self):
        # top border
        move_cursor(self.top, self.left)
        print_border(self.total_cols, BORDER_COLOR)
        for r in range(self.rows):
            # left border
            move_cursor(self.top + 1 + r, self.left)
            print_border(1, BORDER_COLOR)
            # right border
            move_cursor(self.top + 1 + r, self.left + 1 + self.cols)
            print_border(1, BORDER_COLOR)
        # bottom border
        move_cursor(self.top + 1 + self.rows, self.left)
        print_border(self.total_cols, BORDER_COLOR)

        # playing field
        for r in range(self.rows):
            move_cursor(self.top + 1 + r, self.left + 1)
            for color in self.grid[r]:
                if color:
                    print_border(1, color)
                else:
                    output('-')

        # piece
        if self.piece:
            sprite = self.piece['sprites'][self.piece['sprite_idx']]
            x0, y0 = self.piece['x'], self.piece['y']
            for r, cols in enumerate(sprite):
                for c, filled in enumerate(cols):
                    if not filled:
                        continue
                    move_cursor(self.top + 1 + y0 + r, self.left + 1 + x0 + c)
                    print_border(1, self.piece['color'])

    def toggle_pause(self):
        pass

def draw_fps(fps):
    move_cursor(1, 1)
    output_color('FPS:' + str(fps), 31, BORDER_COLOR)

def process_input():
    inp = sys.stdin.buffer.read()
    cmds = {}
    keys = []
    if inp is None:
        return cmds, keys
    i = 0
    while i < len(inp):
        ch = inp[i]
        if ch == ord('\x1b'):
            if i + 2 < len(inp) and inp[i + 1] == ord('['):
                # possibly arrow key, 3 bytes long
                ch = inp[i + 2]
                if ch == ord('A'):
                    keys.append('up')
                elif ch == ord('B'):
                    keys.append('down')
                elif ch == ord('C'):
                    keys.append('right')
                elif ch == ord('D'):
                    keys.append('left')
                i += 3
            else:
                cmds['quit'] = True
                i += 1
        else:
            if ch == ord('p'):
                cmds['pause'] = True
            elif ch == ord('q'):
                cmds['quit'] = True
            i += 1
    return cmds, keys
            

def main():
    # disable terminal echoing and line buffering
    orig_attrs = termios.tcgetattr(sys.stdin)
    @atexit.register
    def restore_attrs():
        termios.tcsetattr(sys.stdin, termios.TCSAFLUSH, orig_attrs)
    tty.setcbreak(sys.stdin.fileno())
    os.set_blocking(sys.stdin.fileno(), False)

    tcols, trows = os.get_terminal_size()
    tetris = Tetris((1, 1, trows, tcols))

    fps = 0
    frames = 0
    t0_fps = time.monotonic()
    t0_update = time.monotonic()
    while True:
        t0_frame = time.monotonic()
        cmds, keys = process_input()

        if cmds.get('pause'):
            tetris.toggle_pause()
        if cmds.get('quit'):
            break

        t1_update = time.monotonic()
        if not tetris.update(t1_update - t0_update, keys):
            break
        t0_update = t1_update

        clear()
        tetris.draw()
        draw_fps(fps)
        flush()

        t1_frame = time.monotonic()
        used_frame_time = t1_frame - t0_frame
        if used_frame_time < FRAME_TIME:
            time.sleep(FRAME_TIME - used_frame_time)

        frames += 1
        t1_fps = time.monotonic()
        dt = t1_fps - t0_fps
        if dt > 1:
            fps = round(frames / dt)
            frames = 0
            t0_fps = t1_fps

main()
