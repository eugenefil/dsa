#!/usr/bin/env python
import time
import random
import os
import sys
import termios
import tty
import atexit
import argparse

TARGET_FPS = 60
FRAME_TIME = 1 / TARGET_FPS
MOVE_SPEED = 1 # blocks/sec
DROP_SPEED = 150 # blocks/sec
NEW_PIECE_TIMEOUT = 0.100
SPEEDUP_TIMEOUT = FRAME_TIME
SPEEDUP_COEF = 20
REMOVE_FILLED_TIMEOUT = NEW_PIECE_TIMEOUT * 2
RESUME_TIMEOUT = 3
GAMEOVER_BLOCK_TIMEOUT = 0.75

EMPTY_COLOR = 40
MIN_PIECE_COLOR = 41
BORDER_COLOR = 47
MAX_PIECE_COLOR = BORDER_COLOR # exclusive
MESSAGE_COLOR = 31

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
    [[[0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]],

     [[0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]],

     [[0, 1, 1, 0],
      [0, 1, 1, 0],
      [0, 0, 0, 0]],

     [[0, 1, 1, 0],
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

def output_color(text, fg=39, bg=49, bold=False):
    bold = ';1' if bold else ''
    print(f'\x1b[{fg};{bg}{bold}m' + text + '\x1b[0m', end='')

class Tetris:
    BORDERS = 2
    MAX_SPRITE_HEIGHT = 4
    MAX_SPRITE_WIDTH = 4
    MAX_MESSAGE_WIDTH = 6 # 'STEADY'

    GAMEINFO_ROWS = (
        2 + # score caption + value
        2 + # level caption + value
        2 + # lines caption + value
        3 # next caption + sprite 0 effective max height (2)
    )
    GAMEINFO_COLS = 5 # max caption width (score, level, lines)

    # game info and playing field share terminal rows
    MIN_FIELD_ROWS = max(MAX_SPRITE_HEIGHT, GAMEINFO_ROWS)
    MAX_FIELD_ROWS = 20
    assert MAX_FIELD_ROWS >= MIN_FIELD_ROWS
    MIN_ROWS = MIN_FIELD_ROWS + BORDERS

    # game info, borders and playing field occupy separate terminal columns
    NON_FIELD_COLS = BORDERS + GAMEINFO_COLS
    MIN_FIELD_COLS = max(MAX_SPRITE_WIDTH, MAX_MESSAGE_WIDTH)
    MAX_FIELD_COLS = 10
    assert MAX_FIELD_COLS >= MIN_FIELD_COLS
    MIN_COLS = MIN_FIELD_COLS + NON_FIELD_COLS

    ASPECT_RATIO = MAX_FIELD_ROWS // MAX_FIELD_COLS

    def __init__(self, screen_rect, no_limits=False,
            active_border_color=BORDER_COLOR, min_piece_color=MIN_PIECE_COLOR):
        top, left, rows, cols = screen_rect
        if rows < self.MIN_ROWS:
            raise ValueError('not enough terminal rows')
        self.field_rows = rows - self.BORDERS
        if not no_limits:
            self.field_rows = min(self.field_rows, self.MAX_FIELD_ROWS)
        self.rows = self.field_rows + self.BORDERS

        if cols < self.MIN_COLS:
            raise ValueError('not enough terminal columns')
        self.field_cols = cols - self.NON_FIELD_COLS
        if not no_limits:
            self.field_cols = min(self.field_cols, self.MAX_FIELD_COLS)
        self.border_cols = self.field_cols + self.BORDERS
        self.cols = self.field_cols + self.NON_FIELD_COLS

        self.top = top + rows // 2 - self.rows // 2
        self.left = left + cols // 2 - self.cols // 2
        self.field_top = self.top + self.BORDERS // 2
        self.field_left = self.left + self.BORDERS // 2
        self.gameinfo_top = (self.field_top + self.field_rows // 2 -
            self.GAMEINFO_ROWS // 2)
        self.gameinfo_left = self.left + self.border_cols

        self.grid = [[None] * self.field_cols for _ in range(self.field_rows)]
        self.decoration_drawn = False
        self.active_border_color = active_border_color
        self.set_active(False) # set border color
        self.min_piece_color = min_piece_color
        self.message_words = None # overlay message like 'GAME OVER'

        self.score = 0
        self.level = 1
        self.lines = 0
        self.gameinfo_changed = True

        self.next_piece = self.new_piece()
        self.make_new_piece()
        self.toggle_pause() # imitate resuming to get READY, STEADY, GO
        self.toggle_pause()

    def new_piece(self):
        sprites = random.choice(PIECES)
        width = len(sprites[0][0])
        height = len(sprites[0])
        return {
            'color': random.choice(range(self.min_piece_color, MAX_PIECE_COLOR)),
            'sprites': sprites,
            'sprite_idx': 0,
            'x': self.field_cols // 2 - width // 2,
            'y': 0,
            'width': width,
            'height': height,
        }

    def check_collision(self):
        sprite = self.piece['sprites'][self.piece['sprite_idx']]
        x0, y0 = self.piece['x'], self.piece['y']
        for r, cols in enumerate(sprite):
            for c, filled in enumerate(cols):
                if not filled:
                    continue
                x = x0 + c
                y = y0 + r
                if x < 0 or x >= self.field_cols or y < 0 or y >= self.field_rows:
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

    def process_gameover(self, dt, keys):
        self.time_to_unblock -= dt
        return True # give up control, we're finished

    def is_blocked(self):
        assert self.is_finished()
        return self.time_to_unblock > 0

    def is_finished(self):
        return self.state == self.process_gameover

    def make_new_piece(self):
        self.speedup_time = 0
        self.drop = False

        self.piece = self.next_piece
        self.next_piece = self.new_piece()
        self.gameinfo_changed = True
        if self.check_collision():
            self.message_words = ['GAME', 'OVER']
            self.state = self.process_gameover
            self.time_to_unblock = GAMEOVER_BLOCK_TIMEOUT
            return True # give up control, game over

        self.state = self.process_move
        self.blocks_traveled = 0

    def process_wait_for_new_piece(self, dt, keys):
        self.time_to_new_piece -= dt
        if self.time_to_new_piece > 0:
            return
        return self.make_new_piece()

    def check_filled_rows(self, y0, height):
        self.filled_rows = []
        for y in range(y0, min(y0 + height, self.field_rows)):
            filled = True
            for block in self.grid[y]:
                if block is None:
                    filled = False
                    break
            if filled:
                self.filled_rows.append(y)
        return len(self.filled_rows) > 0

    def add_score(self, score):
        self.score += score
        self.gameinfo_changed = True

    def add_lines(self, lines):
        self.lines += lines
        self.level = self.lines // 10 + 1
        self.add_score(100 * lines * self.level)

    def process_remove_filled(self, dt, keys):
        self.remove_filled_time -= dt
        if self.remove_filled_time > 0:
            ratio = 1 - self.remove_filled_time / REMOVE_FILLED_TIMEOUT
            blocks_removed = int(ratio * self.field_cols)
            for y in self.filled_rows:
                for x in range(blocks_removed):
                    self.grid[y][x] = None
            return

        assert len(self.filled_rows) > 0
        for y in reversed(self.filled_rows):
            self.grid.pop(y)
        for _ in self.filled_rows:
            self.grid.insert(0, [None] * self.field_cols)
        self.add_lines(len(self.filled_rows))
        self.filled_rows = []
        return self.make_new_piece()

    def process_move(self, dt, keys):
        speed = MOVE_SPEED * 1.25**(self.level - 1)
        if self.speedup_time > 0:
            self.speedup_time -= dt
            speed *= SPEEDUP_COEF
        if self.drop:
            speed = DROP_SPEED # use _fixed_ super fast drop speed instead

        self.blocks_traveled += speed * dt
        if self.blocks_traveled > 1:
            whole_blocks = int(self.blocks_traveled)
            self.blocks_traveled -= whole_blocks
            for _ in range(whole_blocks):
                self.piece['y'] += 1 # try descend
                if self.check_collision():
                    self.piece['y'] -= 1 # collision, revert
                    y0, height = self.piece['y'], self.piece['height']
                    self.consume_piece()
                    if self.check_filled_rows(y0, height):
                        self.state = self.process_remove_filled
                        self.remove_filled_time = REMOVE_FILLED_TIMEOUT
                    else:
                        self.state = self.process_wait_for_new_piece
                        self.time_to_new_piece = NEW_PIECE_TIMEOUT
                    return True # give up control for next tetris

                if speed > MOVE_SPEED:
                    # add point for every block traveled with extra speed
                    self.add_score(1)

                if self.drop:
                    self.drop_trail.append(self.piece['y'])

        # not allowed to navigate when dropping
        if self.drop:
            return

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
            elif key == 'space':
                self.drop = True
                self.drop_trail = []

    def update(self, dt, keys):
        return self.state(dt, keys)

    def draw_piece(self, x0, y0, piece):
        sprite = piece['sprites'][piece['sprite_idx']]
        for r, cols in enumerate(sprite):
            for c, filled in enumerate(cols):
                if not filled:
                    continue
                move_cursor(y0 + r, x0 + c)
                print_border(1, piece['color'])

    def clear_next_piece(self):
        move_cursor(self.gameinfo_top + 7, self.gameinfo_left + 1)
        print_border(4, EMPTY_COLOR)
        move_cursor(self.gameinfo_top + 8, self.gameinfo_left + 1)
        print_border(4, EMPTY_COLOR)

    def draw(self):
        if not self.decoration_drawn:
            self.decoration_drawn = True
            # top border
            move_cursor(self.top, self.left)
            print_border(self.border_cols, self.border_color)
            for r in range(self.field_rows):
                # left border
                move_cursor(self.field_top + r, self.left)
                print_border(1, self.border_color)
                # right border
                move_cursor(self.field_top + r, self.field_left + self.field_cols)
                print_border(1, self.border_color)
            # bottom border
            move_cursor(self.field_top + self.field_rows, self.left)
            print_border(self.border_cols, self.border_color)

            # gameinfo captions
            move_cursor(self.gameinfo_top, self.gameinfo_left)
            output_color('SCORE', bold=True)
            move_cursor(self.gameinfo_top + 2, self.gameinfo_left)
            output_color('LEVEL', bold=True)
            move_cursor(self.gameinfo_top + 4, self.gameinfo_left)
            output_color('LINES', bold=True)
            move_cursor(self.gameinfo_top + 6, self.gameinfo_left)
            output_color('NEXT', bold=True)

        if self.state in (self.process_pause, self.process_resume):
            # resume is considered part of pause here
            if self.clear_field_for_pause:
                self.clear_field_for_pause = False
                # clear playing field
                for y in range(self.field_rows):
                    move_cursor(self.field_top + y, self.field_left)
                    print_border(self.field_cols, EMPTY_COLOR)
                self.clear_next_piece()
        else:
            # playing field
            for r in range(self.field_rows):
                move_cursor(self.field_top + r, self.field_left)
                for color in self.grid[r]:
                    if color:
                        print_border(1, color)
                    else:
                        output('-')

            # piece
            if self.piece:
                x = self.field_left + self.piece['x']
                if self.drop:
                    # drop trail
                    for y in self.drop_trail:
                        self.draw_piece(x, self.field_top + y, self.piece)
                else:
                    self.draw_piece(x, self.field_top + self.piece['y'], self.piece)

            # game info
            if self.gameinfo_changed:
                self.gameinfo_changed = False
                move_cursor(self.gameinfo_top + 1, self.gameinfo_left)
                output_color(f'{self.score:^{self.GAMEINFO_COLS}}', bold=True)
                move_cursor(self.gameinfo_top + 3, self.gameinfo_left)
                output_color(f'{self.level:^{self.GAMEINFO_COLS}}', bold=True)
                move_cursor(self.gameinfo_top + 5, self.gameinfo_left)
                output_color(f'{self.lines:^{self.GAMEINFO_COLS}}', bold=True)

                self.clear_next_piece()
                self.draw_piece(self.gameinfo_left + 1, self.gameinfo_top + 7,
                    self.next_piece)

        # message
        if self.message_words:
            y0 = self.field_rows // 2 - len(self.message_words) // 2
            x0 = self.field_cols // 2 - len(self.message_words[0]) // 2
            for i, word in enumerate(self.message_words):
                move_cursor(self.field_top + y0 + i, self.field_left + x0)
                output_color(word, MESSAGE_COLOR, EMPTY_COLOR, bold=True)

    def set_active(self, active):
        if active:
            self.border_color = self.active_border_color
        else:
            self.border_color = BORDER_COLOR
        self.decoration_drawn = False

    def process_pause(self, dt, keys):
        pass

    def process_resume(self, dt, keys):
        self.time_to_resume -= dt
        if self.time_to_resume > 0:
            i = int(self.time_to_resume / RESUME_TIMEOUT * 6)
            i = min(i, 5) # should be [0, 5] anyway, but playing safe
            if i % 2 > 0:
                # erase message during odd intervals - flashing
                self.message_words = None
            elif i == 4:
                self.message_words = ['READY']
            elif i == 2:
                self.message_words = ['STEADY']
            else:
                self.message_words = ['GO']
            self.clear_field_for_pause = True # clear previous message
            return

        self.message_words = None
        self.gameinfo_changed = True # redraw next piece
        self.state = self.prev_state

    def toggle_pause(self):
        if self.state != self.process_pause:
            self.message_words = ['PAUSE']
            self.clear_field_for_pause = True # hide playing field during pause
            if self.state != self.process_resume:
                # if we're pausing from resume, previous state is already saved
                self.prev_state = self.state
            self.state = self.process_pause
        else:
            self.state = self.process_resume
            self.time_to_resume = RESUME_TIMEOUT


def draw_fps(fps):
    move_cursor(1, 1)
    output_color(f'FPS:{fps:<2}', MESSAGE_COLOR, BORDER_COLOR)

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
            elif ch == ord(' '):
                keys.append('space')
            i += 1
    return cmds, keys
            
def parse_args():
    parser = argparse.ArgumentParser(
        description='Terminal tetris.'
    )
    parser.add_argument('--no-limits',
        help='Disable restrictions on max playing field size',
        action='store_true',
    )
    def positive_integer(s):
        x = int(s)
        if x <= 0:
            raise ValueError
        return x
    parser.add_argument('--num',
        help='Play simultaneously on NUM playing fields',
        type=positive_integer,
        default=1,
    )
    return parser.parse_args()

# for a given total number of tetrises on the screen brute-force search
# the best combination of rows x cols those tetrises must occupy, while
# respecting the desirable tetris aspect ratio as much as possible
def find_tetris_matrix(total_tetrises, screen_height, screen_width):
    def lossfn(rows_of_tetrises, cols_of_tetrises):
        tetris_height = screen_height / rows_of_tetrises
        tetris_width = screen_width / cols_of_tetrises
        tetris_aspect = tetris_height / tetris_width
        # penalize for deviating from aspect ratio and total num of tetrises
        return ((tetris_aspect - Tetris.ASPECT_RATIO)**2 +
            (rows_of_tetrises * cols_of_tetrises - total_tetrises)**2)

    max_rows_of_tetrises = screen_height // Tetris.MIN_ROWS
    max_cols_of_tetrises = screen_width // Tetris.MIN_COLS
    best = (0, 0)
    min_loss = 10000000
    for rows in range(1, max_rows_of_tetrises + 1):
        for cols in range(1, max_cols_of_tetrises + 1):
            if rows * cols < total_tetrises:
                continue
            loss = lossfn(rows, cols)
            if loss < min_loss:
                min_loss = loss
                best = (rows, cols)
    return best

def create_tetrises(args):
    screen_width, screen_height = os.get_terminal_size()
    if args.num == 1:
        return [Tetris(
            (1, 1, screen_height, screen_width),
            no_limits=args.no_limits
        )]

    rows, cols = find_tetris_matrix(args.num, screen_height, screen_width)
    if rows == 0 or cols == 0:
        raise ValueError(f'cannot pack {args.num} tetrises')
    tetris_height = screen_height // rows
    tetris_width = screen_width // cols

    vfiller = 0 # vertical space between tetrises
    if rows > 1:
        vfiller = (screen_height - tetris_height * rows) // (rows - 1)
    hfiller = 0 # horizontal space between tetrises
    if cols > 1:
        hfiller = (screen_width - tetris_width * cols) // (cols - 1)

    tetrises = []
    for r in range(rows):
        for c in range(cols):
            screen_rect = (
                1 + r * (tetris_height + vfiller),
                1 + c * (tetris_width + hfiller),
                tetris_height,
                tetris_width
            )
            tetrises.append(Tetris(screen_rect,
                # borrow min piece color to draw borders of active tetris
                active_border_color=MIN_PIECE_COLOR,
                min_piece_color=MIN_PIECE_COLOR + 1,
                no_limits=args.no_limits))
            if len(tetrises) == args.num:
                return tetrises
    return tetrises

def setup_terminal():
    # disable terminal echoing and line buffering
    orig_attrs = termios.tcgetattr(sys.stdin)
    @atexit.register
    def restore_attrs():
        termios.tcsetattr(sys.stdin, termios.TCSAFLUSH, orig_attrs)
    tty.setcbreak(sys.stdin.fileno())
    os.set_blocking(sys.stdin.fileno(), False)

def init_tetrises(args):
    tetrises = create_tetrises(args)
    active_tetris = 0
    tetrises[active_tetris].set_active(True)
    clear()
    return tetrises, active_tetris

def main():
    setup_terminal()
    args = parse_args()
    tetrises, active_tetris = init_tetrises(args)
    fps = 0
    frames = 0
    t0_fps = time.monotonic()
    t0_update = time.monotonic()
    while True:
        t0_frame = time.monotonic()
        cmds, keys = process_input()
        if cmds.get('quit'):
            break
        if cmds.get('pause'):
            for tetris in tetrises:
                tetris.toggle_pause()

        t1_update = time.monotonic()
        dt = t1_update - t0_update
        num_finished = 0
        active_done = False
        for i, tetris in enumerate(tetrises):
            if i == active_tetris:
                active_done = tetris.update(dt, keys)
            else:
                tetris.update(dt, [])
            num_finished += int(tetris.is_finished())
        t0_update = t1_update

        if num_finished == len(tetrises):
            if len(keys) > 0 and not tetrises[active_tetris].is_blocked():
                # restart on key press
                tetrises, active_tetris = init_tetrises(args)
        elif active_done:
            # find next unfinished tetris and activate it
            next_active = None
            for offset in range(1, len(tetrises)):
                i = (active_tetris + offset) % len(tetrises)
                if not tetrises[i].is_finished():
                    next_active = i
                    break
            if next_active is not None:
                assert next_active != active_tetris
                tetrises[active_tetris].set_active(False)
                tetrises[next_active].set_active(True)
                active_tetris = next_active

        for tetris in tetrises:
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
