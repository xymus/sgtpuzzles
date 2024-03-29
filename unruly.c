/*
 * unruly.c: Implementation for Binary Puzzles.
 * (C) 2012 Lennard Sprong
 * Created for Simon Tatham's Portable Puzzle Collection
 * See LICENCE for licence details
 *
 * Objective of the game: Fill the grid with zeros and ones, with the
 * following rules:
 * - There can't be a run of three or more equal numbers.
 * - Each row and column contains an equal amount of zeros and ones.
 *
 * This puzzle type is known under several names, including
 * Tohu-Wa-Vohu, One and Two and Binairo.
 *
 * Some variants include an extra constraint, stating that no two rows or two
 * columns may contain the same exact sequence of zeros and ones.
 * This rule is rarely used, so it has been discarded for this implementation.
 *
 * More information:
 * http://www.janko.at/Raetsel/Tohu-Wa-Vohu/index.htm
 */

/*
 * Possible future improvements:
 *
 * More solver cleverness
 *
 *  - a counting-based deduction in which you find groups of squares
 *    which must each contain at least one of a given colour, plus
 *    other squares which are already known to be that colour, and see
 *    if you have any squares left over when you've worked out where
 *    they all have to be. This is a generalisation of the current
 *    check_near_complete: where that only covers rows with three
 *    unfilled squares, this would handle more, such as
 *        0 . . 1 0 1 . . 0 .
 *    in which each of the two-square gaps must contain a 0, and there
 *    are three 0s placed, and that means the rightmost square can't
 *    be a 0.
 *
 *  - an 'Unreasonable' difficulty level, supporting recursion and
 *    backtracking.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <math.h>

#include "puzzles.h"

#ifdef STANDALONE_SOLVER
int solver_verbose = FALSE;
#endif

enum {
    COL_BACKGROUND,
    COL_GRID,
    COL_EMPTY,
    /*
     * When editing this enum, maintain the invariants
     *   COL_n_HIGHLIGHT = COL_n + 1
     *   COL_n_LOWLIGHT = COL_n + 2
     */
    COL_0,
    COL_0_HIGHLIGHT,
    COL_0_LOWLIGHT,
    COL_1,
    COL_1_HIGHLIGHT,
    COL_1_LOWLIGHT,
    COL_CURSOR,
    COL_ERROR,
    NCOLOURS
};

struct game_params {
    int w2, h2;        /* full grid width and height respectively */
    int diff;
};
#define DIFFLIST(A)                             \
    A(EASY,Easy, e)                             \
    A(NORMAL,Normal, n)                         \

#define ENUM(upper,title,lower) DIFF_ ## upper,
#define TITLE(upper,title,lower) #title,
#define ENCODE(upper,title,lower) #lower
#define CONFIG(upper,title,lower) ":" #title
enum { DIFFLIST(ENUM) DIFFCOUNT };
static char const *const unruly_diffnames[] = { DIFFLIST(TITLE) };

static char const unruly_diffchars[] = DIFFLIST(ENCODE);
#define DIFFCONFIG DIFFLIST(CONFIG)

const static struct game_params unruly_presets[] = {
    { 8,  8, DIFF_EASY},
    { 8,  8, DIFF_NORMAL},
    {10, 10, DIFF_EASY},
    {10, 10, DIFF_NORMAL},
    {14, 14, DIFF_EASY},
    {14, 14, DIFF_NORMAL}
};

#define DEFAULT_PRESET 0

enum {
    EMPTY,
    N_ONE,
    N_ZERO,
    BOGUS
};

#define FE_HOR_ROW_LEFT   0x001
#define FE_HOR_ROW_MID    0x003
#define FE_HOR_ROW_RIGHT  0x002

#define FE_VER_ROW_TOP    0x004
#define FE_VER_ROW_MID    0x00C
#define FE_VER_ROW_BOTTOM 0x008

#define FE_COUNT          0x010

#define FF_ONE            0x020
#define FF_ZERO           0x040
#define FF_CURSOR         0x080

#define FF_FLASH1         0x100
#define FF_FLASH2         0x200
#define FF_IMMUTABLE      0x400

struct game_state {
    int w2, h2;
    char *grid;
    unsigned char *immutable;

    int completed, cheated;
};

static game_params *default_params(void)
{
    game_params *ret = snew(game_params);

    *ret = unruly_presets[DEFAULT_PRESET];        /* structure copy */

    return ret;
}

static int game_fetch_preset(int i, char **name, game_params **params)
{
    game_params *ret;
    char buf[80];

    if (i < 0 || i >= lenof(unruly_presets))
        return FALSE;

    ret = snew(game_params);
    *ret = unruly_presets[i];     /* structure copy */

    sprintf(buf, "%dx%d %s", ret->w2, ret->h2, unruly_diffnames[ret->diff]);

    *name = dupstr(buf);
    *params = ret;
    return TRUE;
}

static void free_params(game_params *params)
{
    sfree(params);
}

static game_params *dup_params(game_params *params)
{
    game_params *ret = snew(game_params);
    *ret = *params;             /* structure copy */
    return ret;
}

static void decode_params(game_params *params, char const *string)
{
    char const *p = string;

    params->w2 = atoi(p);
    while (*p && isdigit((unsigned char)*p)) p++;
    if (*p == 'x') {
        p++;
        params->h2 = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
    } else {
        params->h2 = params->w2;
    }

    if (*p == 'd') {
        int i;
        p++;
        params->diff = DIFFCOUNT + 1;   /* ...which is invalid */
        if (*p) {
            for (i = 0; i < DIFFCOUNT; i++) {
                if (*p == unruly_diffchars[i])
                    params->diff = i;
            }
            p++;
        }
    }
}

static char *encode_params(game_params *params, int full)
{
    char buf[80];

    sprintf(buf, "%dx%d", params->w2, params->h2);
    if (full)
        sprintf(buf + strlen(buf), "d%c", unruly_diffchars[params->diff]);

    return dupstr(buf);
}

static config_item *game_configure(game_params *params)
{
    config_item *ret;
    char buf[80];

    ret = snewn(4, config_item);

    ret[0].name = "Width";
    ret[0].type = C_STRING;
    sprintf(buf, "%d", params->w2);
    ret[0].sval = dupstr(buf);
    ret[0].ival = 0;

    ret[1].name = "Height";
    ret[1].type = C_STRING;
    sprintf(buf, "%d", params->h2);
    ret[1].sval = dupstr(buf);
    ret[1].ival = 0;

    ret[2].name = "Difficulty";
    ret[2].type = C_CHOICES;
    ret[2].sval = DIFFCONFIG;
    ret[2].ival = params->diff;

    ret[3].name = NULL;
    ret[3].type = C_END;
    ret[3].sval = NULL;
    ret[3].ival = 0;

    return ret;
}

static game_params *custom_params(config_item *cfg)
{
    game_params *ret = snew(game_params);

    ret->w2 = atoi(cfg[0].sval);
    ret->h2 = atoi(cfg[1].sval);
    ret->diff = cfg[2].ival;

    return ret;
}

static char *validate_params(game_params *params, int full)
{
    if ((params->w2 & 1) || (params->h2 & 1))
        return "Width and height must both be even";
    if (params->w2 < 6 || params->h2 < 6)
        return "Width and height must be at least 6";
    if (params->diff >= DIFFCOUNT)
        return "Unknown difficulty rating";

    return NULL;
}

static char *validate_desc(game_params *params, char *desc)
{
    int w2 = params->w2, h2 = params->h2;
    int s = w2 * h2;

    char *p = desc;
    int pos = 0;

    while (*p) {
        if (*p >= 'a' && *p < 'z')
            pos += 1 + (*p - 'a');
        else if (*p >= 'A' && *p < 'Z')
            pos += 1 + (*p - 'A');
        else if (*p == 'Z' || *p == 'z')
            pos += 25;
        else
            return "Description contains invalid characters";

        ++p;
    }

    if (pos < s+1)
        return "Description too short";
    if (pos > s+1)
        return "Description too long";

    return NULL;
}

static game_state *blank_state(int w2, int h2)
{
    game_state *state = snew(game_state);
    int s = w2 * h2;

    state->w2 = w2;
    state->h2 = h2;
    state->grid = snewn(s, char);
    state->immutable = snewn(s, unsigned char);

    memset(state->grid, EMPTY, s);
    memset(state->immutable, FALSE, s);

    state->completed = state->cheated = FALSE;

    return state;
}

static game_state *new_game(midend *me, game_params *params, char *desc)
{
    int w2 = params->w2, h2 = params->h2;
    int s = w2 * h2;

    game_state *state = blank_state(w2, h2);

    char *p = desc;
    int pos = 0;

    while (*p) {
        if (*p >= 'a' && *p < 'z') {
            pos += (*p - 'a');
            if (pos < s) {
                state->grid[pos] = N_ZERO;
                state->immutable[pos] = TRUE;
            }
            pos++;
        } else if (*p >= 'A' && *p < 'Z') {
            pos += (*p - 'A');
            if (pos < s) {
                state->grid[pos] = N_ONE;
                state->immutable[pos] = TRUE;
            }
            pos++;
        } else if (*p == 'Z' || *p == 'z') {
            pos += 25;
        } else
            assert(!"Description contains invalid characters");

        ++p;
    }
    assert(pos == s+1);

    return state;
}

static game_state *dup_game(game_state *state)
{
    int w2 = state->w2, h2 = state->h2;
    int s = w2 * h2;

    game_state *ret = blank_state(w2, h2);

    memcpy(ret->grid, state->grid, s);
    memcpy(ret->immutable, state->immutable, s);

    ret->completed = state->completed;
    ret->cheated = state->cheated;

    return ret;
}

static void free_game(game_state *state)
{
    sfree(state->grid);
    sfree(state->immutable);

    sfree(state);
}

static int game_can_format_as_text_now(game_params *params)
{
    return TRUE;
}

static char *game_text_format(game_state *state)
{
    int w2 = state->w2, h2 = state->h2;
    int lr = w2*2 + 1;

    char *ret = snewn(lr * h2 + 1, char);
    char *p = ret;

    int x, y;
    for (y = 0; y < h2; y++) {
        for (x = 0; x < w2; x++) {
            /* Place number */
            char c = state->grid[y * w2 + x];
            *p++ = (c == N_ONE ? '1' : c == N_ZERO ? '0' : '.');
            *p++ = ' ';
        }
        /* End line */
        *p++ = '\n';
    }
    /* End with NUL */
    *p++ = '\0';

    return ret;
}

/* ****** *
 * Solver *
 * ****** */

struct unruly_scratch {
    int *ones_rows;
    int *ones_cols;
    int *zeros_rows;
    int *zeros_cols;
};

static void unruly_solver_update_remaining(game_state *state,
                                           struct unruly_scratch *scratch)
{
    int w2 = state->w2, h2 = state->h2;
    int x, y;

    /* Reset all scratch data */
    memset(scratch->ones_rows, 0, h2 * sizeof(int));
    memset(scratch->ones_cols, 0, w2 * sizeof(int));
    memset(scratch->zeros_rows, 0, h2 * sizeof(int));
    memset(scratch->zeros_cols, 0, w2 * sizeof(int));

    for (x = 0; x < w2; x++)
        for (y = 0; y < h2; y++) {
            if (state->grid[y * w2 + x] == N_ONE) {
                scratch->ones_rows[y]++;
                scratch->ones_cols[x]++;
            } else if (state->grid[y * w2 + x] == N_ZERO) {
                scratch->zeros_rows[y]++;
                scratch->zeros_cols[x]++;
            }
        }
}

static struct unruly_scratch *unruly_new_scratch(game_state *state)
{
    int w2 = state->w2, h2 = state->h2;

    struct unruly_scratch *ret = snew(struct unruly_scratch);

    ret->ones_rows = snewn(h2, int);
    ret->ones_cols = snewn(w2, int);
    ret->zeros_rows = snewn(h2, int);
    ret->zeros_cols = snewn(w2, int);

    unruly_solver_update_remaining(state, ret);

    return ret;
}

static void unruly_free_scratch(struct unruly_scratch *scratch)
{
    sfree(scratch->ones_rows);
    sfree(scratch->ones_cols);
    sfree(scratch->zeros_rows);
    sfree(scratch->zeros_cols);

    sfree(scratch);
}

static int unruly_solver_check_threes(game_state *state, int *rowcount,
                                      int *colcount, int horizontal,
                                      char check, char block)
{
    int w2 = state->w2, h2 = state->h2;

    int dx = horizontal ? 1 : 0, dy = 1 - dx;
    int sx = dx, sy = dy;
    int ex = w2 - dx, ey = h2 - dy;

    int x, y;
    int ret = 0;

    /* Check for any three squares which almost form three in a row */
    for (y = sy; y < ey; y++) {
        for (x = sx; x < ex; x++) {
            int i1 = (y-dy) * w2 + (x-dx);
            int i2 = y * w2 + x;
            int i3 = (y+dy) * w2 + (x+dx);

            if (state->grid[i1] == check && state->grid[i2] == check
                && state->grid[i3] == EMPTY) {
                ret++;
#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: %i,%i and %i,%i confirm %c at %i,%i\n",
                           i1 % w2, i1 / w2, i2 % w2, i2 / w2,
                           (block == N_ONE ? '1' : '0'), i3 % w2,
                           i3 / w2);
                }
#endif
                state->grid[i3] = block;
                rowcount[i3 / w2]++;
                colcount[i3 % w2]++;
            }
            if (state->grid[i1] == check && state->grid[i2] == EMPTY
                && state->grid[i3] == check) {
                ret++;
#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: %i,%i and %i,%i confirm %c at %i,%i\n",
                           i1 % w2, i1 / w2, i3 % w2, i3 / w2,
                           (block == N_ONE ? '1' : '0'), i2 % w2,
                           i2 / w2);
                }
#endif
                state->grid[i2] = block;
                rowcount[i2 / w2]++;
                colcount[i2 % w2]++;
            }
            if (state->grid[i1] == EMPTY && state->grid[i2] == check
                && state->grid[i3] == check) {
                ret++;
#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: %i,%i and %i,%i confirm %c at %i,%i\n",
                           i2 % w2, i2 / w2, i3 % w2, i3 / w2,
                           (block == N_ONE ? '1' : '0'), i1 % w2,
                           i1 / w2);
                }
#endif
                state->grid[i1] = block;
                rowcount[i1 / w2]++;
                colcount[i1 % w2]++;
            }
        }
    }

    return ret;
}

static int unruly_solver_check_all_threes(game_state *state,
                                          struct unruly_scratch *scratch)
{
    int ret = 0;

    ret +=
        unruly_solver_check_threes(state, scratch->zeros_rows,
                                   scratch->zeros_cols, TRUE, N_ONE, N_ZERO);
    ret +=
        unruly_solver_check_threes(state, scratch->ones_rows,
                                   scratch->ones_cols, TRUE, N_ZERO, N_ONE);
    ret +=
        unruly_solver_check_threes(state, scratch->zeros_rows,
                                   scratch->zeros_cols, FALSE, N_ONE,
                                   N_ZERO);
    ret +=
        unruly_solver_check_threes(state, scratch->ones_rows,
                                   scratch->ones_cols, FALSE, N_ZERO, N_ONE);

    return ret;
}

static int unruly_solver_fill_row(game_state *state, int i, int horizontal,
                                  int *rowcount, int *colcount, char fill)
{
    int ret = 0;
    int w2 = state->w2, h2 = state->h2;
    int j;

#ifdef STANDALONE_SOLVER
    if (solver_verbose) {
        printf("Solver: Filling %s %i with %c:",
               (horizontal ? "Row" : "Col"), i,
               (fill == N_ZERO ? '0' : '1'));
    }
#endif
    /* Place a number in every empty square in a row/column */
    for (j = 0; j < (horizontal ? w2 : h2); j++) {
        int p = (horizontal ? i * w2 + j : j * w2 + i);

        if (state->grid[p] == EMPTY) {
#ifdef STANDALONE_SOLVER
            if (solver_verbose) {
                printf(" (%i,%i)", (horizontal ? j : i),
                       (horizontal ? i : j));
            }
#endif
            ret++;
            state->grid[p] = fill;
            rowcount[(horizontal ? i : j)]++;
            colcount[(horizontal ? j : i)]++;
        }
    }

#ifdef STANDALONE_SOLVER
    if (solver_verbose) {
        printf("\n");
    }
#endif

    return ret;
}

static int unruly_solver_check_complete_nums(game_state *state,
                                             int *complete, int horizontal,
                                             int *rowcount, int *colcount,
                                             char fill)
{
    int w2 = state->w2, h2 = state->h2;
    int count = (horizontal ? h2 : w2); /* number of rows to check */
    int target = (horizontal ? w2 : h2) / 2; /* target number of 0s/1s */
    int *other = (horizontal ? rowcount : colcount);

    int ret = 0;

    int i;
    /* Check for completed rows/cols for one number, then fill in the rest */
    for (i = 0; i < count; i++) {
        if (complete[i] == target && other[i] < target) {
#ifdef STANDALONE_SOLVER
            if (solver_verbose) {
                printf("Solver: Row %i satisfied for %c\n", i,
                       (fill != N_ZERO ? '0' : '1'));
            }
#endif
            ret += unruly_solver_fill_row(state, i, horizontal, rowcount,
                                          colcount, fill);
        }
    }

    return ret;
}

static int unruly_solver_check_all_complete_nums(game_state *state,
                                                 struct unruly_scratch *scratch)
{
    int ret = 0;

    ret +=
        unruly_solver_check_complete_nums(state, scratch->ones_rows, TRUE,
                                          scratch->zeros_rows,
                                          scratch->zeros_cols, N_ZERO);
    ret +=
        unruly_solver_check_complete_nums(state, scratch->ones_cols, FALSE,
                                          scratch->zeros_rows,
                                          scratch->zeros_cols, N_ZERO);
    ret +=
        unruly_solver_check_complete_nums(state, scratch->zeros_rows, TRUE,
                                          scratch->ones_rows,
                                          scratch->ones_cols, N_ONE);
    ret +=
        unruly_solver_check_complete_nums(state, scratch->zeros_cols, FALSE,
                                          scratch->ones_rows,
                                          scratch->ones_cols, N_ONE);

    return ret;
}

static int unruly_solver_check_near_complete(game_state *state,
                                             int *complete, int horizontal,
                                             int *rowcount, int *colcount,
                                             char fill)
{
    int w2 = state->w2, h2 = state->h2;
    int w = w2/2, h = h2/2;

    int dx = horizontal ? 1 : 0, dy = 1 - dx;

    int sx = dx, sy = dy;
    int ex = w2 - dx, ey = h2 - dy;

    int x, y;
    int ret = 0;

    /*
     * This function checks for a row with one Y remaining, then looks
     * for positions that could cause the remaining squares in the row
     * to make 3 X's in a row. Example:
     *
     * Consider the following row:
     * 1 1 0 . . .
     * If the last 1 was placed in the last square, the remaining
     * squares would be 0:
     * 1 1 0 0 0 1
     * This violates the 3 in a row rule. We now know that the last 1
     * shouldn't be in the last cell.
     * 1 1 0 . . 0
     */

    /* Check for any two blank and one filled square */
    for (y = sy; y < ey; y++) {
        /* One type must have 1 remaining, the other at least 2 */
        if (horizontal && (complete[y] < w - 1 || rowcount[y] > w - 2))
            continue;

        for (x = sx; x < ex; x++) {
            int i, i1, i2, i3;
            if (!horizontal
                && (complete[x] < h - 1 || colcount[x] > h - 2))
                continue;

            i = (horizontal ? y : x);
            i1 = (y-dy) * w2 + (x-dx);
            i2 = y * w2 + x;
            i3 = (y+dy) * w2 + (x+dx);

            if (state->grid[i1] == fill && state->grid[i2] == EMPTY
                && state->grid[i3] == EMPTY) {
                /*
                 * Temporarily fill the empty spaces with something else.
                 * This avoids raising the counts for the row and column
                 */
                state->grid[i2] = BOGUS;
                state->grid[i3] = BOGUS;

#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: Row %i nearly satisfied for %c\n", i,
                           (fill != N_ZERO ? '0' : '1'));
                }
#endif
                ret +=
                    unruly_solver_fill_row(state, i, horizontal, rowcount,
                                         colcount, fill);

                state->grid[i2] = EMPTY;
                state->grid[i3] = EMPTY;
            }

            else if (state->grid[i1] == EMPTY && state->grid[i2] == fill
                     && state->grid[i3] == EMPTY) {
                state->grid[i1] = BOGUS;
                state->grid[i3] = BOGUS;

#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: Row %i nearly satisfied for %c\n", i,
                           (fill != N_ZERO ? '0' : '1'));
                }
#endif
                ret +=
                    unruly_solver_fill_row(state, i, horizontal, rowcount,
                                         colcount, fill);

                state->grid[i1] = EMPTY;
                state->grid[i3] = EMPTY;
            }

            else if (state->grid[i1] == EMPTY && state->grid[i2] == EMPTY
                     && state->grid[i3] == fill) {
                state->grid[i1] = BOGUS;
                state->grid[i2] = BOGUS;

#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: Row %i nearly satisfied for %c\n", i,
                           (fill != N_ZERO ? '0' : '1'));
                }
#endif
                ret +=
                    unruly_solver_fill_row(state, i, horizontal, rowcount,
                                         colcount, fill);

                state->grid[i1] = EMPTY;
                state->grid[i2] = EMPTY;
            }

            else if (state->grid[i1] == EMPTY && state->grid[i2] == EMPTY
                     && state->grid[i3] == EMPTY) {
                state->grid[i1] = BOGUS;
                state->grid[i2] = BOGUS;
                state->grid[i3] = BOGUS;

#ifdef STANDALONE_SOLVER
                if (solver_verbose) {
                    printf("Solver: Row %i nearly satisfied for %c\n", i,
                           (fill != N_ZERO ? '0' : '1'));
                }
#endif
                ret +=
                    unruly_solver_fill_row(state, i, horizontal, rowcount,
                                         colcount, fill);

                state->grid[i1] = EMPTY;
                state->grid[i2] = EMPTY;
                state->grid[i3] = EMPTY;
            }
        }
    }

    return ret;
}

static int unruly_solver_check_all_near_complete(game_state *state,
                                                 struct unruly_scratch *scratch)
{
    int ret = 0;

    ret +=
        unruly_solver_check_near_complete(state, scratch->ones_rows, TRUE,
                                        scratch->zeros_rows,
                                        scratch->zeros_cols, N_ZERO);
    ret +=
        unruly_solver_check_near_complete(state, scratch->ones_cols, FALSE,
                                        scratch->zeros_rows,
                                        scratch->zeros_cols, N_ZERO);
    ret +=
        unruly_solver_check_near_complete(state, scratch->zeros_rows, TRUE,
                                        scratch->ones_rows,
                                        scratch->ones_cols, N_ONE);
    ret +=
        unruly_solver_check_near_complete(state, scratch->zeros_cols, FALSE,
                                        scratch->ones_rows,
                                        scratch->ones_cols, N_ONE);

    return ret;
}

static int unruly_validate_rows(game_state *state, int horizontal,
                                char check, int *errors)
{
    int w2 = state->w2, h2 = state->h2;

    int dx = horizontal ? 1 : 0, dy = 1 - dx;

    int sx = dx, sy = dy;
    int ex = w2 - dx, ey = h2 - dy;

    int x, y;
    int ret = 0;

    int err1 = (horizontal ? FE_HOR_ROW_LEFT : FE_VER_ROW_TOP);
    int err2 = (horizontal ? FE_HOR_ROW_MID : FE_VER_ROW_MID);
    int err3 = (horizontal ? FE_HOR_ROW_RIGHT : FE_VER_ROW_BOTTOM);

    /* Check for any three in a row, and mark errors accordingly (if
     * required) */
    for (y = sy; y < ey; y++) {
        for (x = sx; x < ex; x++) {
            int i1 = (y-dy) * w2 + (x-dx);
            int i2 = y * w2 + x;
            int i3 = (y+dy) * w2 + (x+dx);

            if (state->grid[i1] == check && state->grid[i2] == check
                && state->grid[i3] == check) {
                ret++;
                if (errors) {
                    errors[i1] |= err1;
                    errors[i2] |= err2;
                    errors[i3] |= err3;
                }
            }
        }
    }

    return ret;
}

static int unruly_validate_all_rows(game_state *state, int *errors)
{
    int errcount = 0;

    errcount += unruly_validate_rows(state, TRUE, N_ONE, errors);
    errcount += unruly_validate_rows(state, FALSE, N_ONE, errors);
    errcount += unruly_validate_rows(state, TRUE, N_ZERO, errors);
    errcount += unruly_validate_rows(state, FALSE, N_ZERO, errors);

    if (errcount)
        return -1;
    return 0;
}

static int unruly_validate_counts(game_state *state,
                                  struct unruly_scratch *scratch, int *errors)
{
    int w2 = state->w2, h2 = state->h2;
    int w = w2/2, h = h2/2;
    char below = FALSE;
    char above = FALSE;
    int i;

    /* See if all rows/columns are satisfied. If one is exceeded,
     * mark it as an error (if required)
     */

    char hasscratch = TRUE;
    if (!scratch) {
        scratch = unruly_new_scratch(state);
        hasscratch = FALSE;
    }

    for (i = 0; i < w2; i++) {
        if (scratch->ones_cols[i] < h)
            below = TRUE;
        if (scratch->zeros_cols[i] < h)
            below = TRUE;

        if (scratch->ones_cols[i] > h) {
            above = TRUE;
            if (errors)
                errors[2*h2 + i] = TRUE;
        } else if (errors)
            errors[2*h2 + i] = FALSE;

        if (scratch->zeros_cols[i] > h) {
            above = TRUE;
            if (errors)
                errors[2*h2 + w2 + i] = TRUE;
        } else if (errors)
            errors[2*h2 + w2 + i] = FALSE;
    }
    for (i = 0; i < h2; i++) {
        if (scratch->ones_rows[i] < w)
            below = TRUE;
        if (scratch->zeros_rows[i] < w)
            below = TRUE;

        if (scratch->ones_rows[i] > w) {
            above = TRUE;
            if (errors)
                errors[i] = TRUE;
        } else if (errors)
            errors[i] = FALSE;

        if (scratch->zeros_rows[i] > w) {
            above = TRUE;
            if (errors)
                errors[h2 + i] = TRUE;
        } else if (errors)
            errors[h2 + i] = FALSE;
    }

    if (!hasscratch)
        unruly_free_scratch(scratch);

    return (above ? -1 : below ? 1 : 0);
}

static int unruly_solve_game(game_state *state,
                             struct unruly_scratch *scratch, int diff)
{
    int done, maxdiff = -1;

    while (TRUE) {
        done = 0;

        /* Check for impending 3's */
        done += unruly_solver_check_all_threes(state, scratch);

        /* Keep using the simpler techniques while they produce results */
        if (done) {
            if (maxdiff < DIFF_EASY)
                maxdiff = DIFF_EASY;
            continue;
        }

        /* Check for completed rows */
        done += unruly_solver_check_all_complete_nums(state, scratch);

        if (done) {
            if (maxdiff < DIFF_EASY)
                maxdiff = DIFF_EASY;
            continue;
        }

        /* Normal techniques */
        if (diff < DIFF_NORMAL)
            break;

        /* Check for nearly completed rows */
        done += unruly_solver_check_all_near_complete(state, scratch);

        if (done) {
            if (maxdiff < DIFF_NORMAL)
                maxdiff = DIFF_NORMAL;
            continue;
        }

        break;
    }
    return maxdiff;
}

static char *solve_game(game_state *state, game_state *currstate,
                        char *aux, char **error)
{
    game_state *solved = dup_game(state);
    struct unruly_scratch *scratch = unruly_new_scratch(solved);
    char *ret = NULL;
    int result;

    unruly_solve_game(solved, scratch, DIFFCOUNT);

    result = unruly_validate_counts(solved, scratch, NULL);
    if (unruly_validate_all_rows(solved, NULL) == -1)
        result = -1;

    if (result == 0) {
        int w2 = solved->w2, h2 = solved->h2;
        int s = w2 * h2;
        char *p;
        int i;

        ret = snewn(s + 2, char);
        p = ret;
        *p++ = 'S';

        for (i = 0; i < s; i++)
            *p++ = (solved->grid[i] == N_ONE ? '1' : '0');

        *p++ = '\0';
    } else if (result == 1)
        *error = "No solution found.";
    else if (result == -1)
        *error = "Puzzle is invalid.";

    free_game(solved);
    unruly_free_scratch(scratch);
    return ret;
}

/* ********* *
 * Generator *
 * ********* */

static int unruly_fill_game(game_state *state, struct unruly_scratch *scratch,
                            random_state *rs)
{

    int w2 = state->w2, h2 = state->h2;
    int s = w2 * h2;
    int i, j;
    int *spaces;

#ifdef STANDALONE_SOLVER
    if (solver_verbose) {
        printf("Generator: Attempt to fill grid\n");
    }
#endif

    /* Generate random array of spaces */
    spaces = snewn(s, int);
    for (i = 0; i < s; i++)
        spaces[i] = i;
    shuffle(spaces, s, sizeof(*spaces), rs);

    /*
     * Construct a valid filled grid by repeatedly picking an unfilled
     * space and fill it, then calling the solver to fill in any
     * spaces forced by the change.
     */
    for (j = 0; j < s; j++) {
        i = spaces[j];

        if (state->grid[i] != EMPTY)
            continue;

        if (random_upto(rs, 2)) {
            state->grid[i] = N_ONE;
            scratch->ones_rows[i / w2]++;
            scratch->ones_cols[i % w2]++;
        } else {
            state->grid[i] = N_ZERO;
            scratch->zeros_rows[i / w2]++;
            scratch->zeros_cols[i % w2]++;
        }

        unruly_solve_game(state, scratch, DIFFCOUNT);
    }
    sfree(spaces);

    if (unruly_validate_all_rows(state, NULL) != 0
        || unruly_validate_counts(state, scratch, NULL) != 0)
        return FALSE;

    return TRUE;
}

static char *new_game_desc(game_params *params, random_state *rs,
                           char **aux, int interactive)
{
#ifdef STANDALONE_SOLVER
    char *debug;
    int temp_verbose = FALSE;
#endif

    int w2 = params->w2, h2 = params->h2;
    int s = w2 * h2;
    int *spaces;
    int i, j, run;
    char *ret, *p;

    game_state *state;
    struct unruly_scratch *scratch;

    int attempts = 0;

    while (1) {

        while (TRUE) {
            attempts++;
            state = blank_state(w2, h2);
            scratch = unruly_new_scratch(state);
            if (unruly_fill_game(state, scratch, rs))
                break;
            free_game(state);
            unruly_free_scratch(scratch);
        }

#ifdef STANDALONE_SOLVER
        if (solver_verbose) {
            printf("Puzzle generated in %i attempts\n", attempts);
            debug = game_text_format(state);
            fputs(debug, stdout);
            sfree(debug);

            temp_verbose = solver_verbose;
            solver_verbose = FALSE;
        }
#endif

        unruly_free_scratch(scratch);

        /* Generate random array of spaces */
        spaces = snewn(s, int);
        for (i = 0; i < s; i++)
            spaces[i] = i;
        shuffle(spaces, s, sizeof(*spaces), rs);

        /*
         * Winnow the clues by starting from our filled grid, repeatedly
         * picking a filled space and emptying it, as long as the solver
         * reports that the puzzle can still be solved after doing so.
         */
        for (j = 0; j < s; j++) {
            char c;
            game_state *solver;

            i = spaces[j];

            c = state->grid[i];
            state->grid[i] = EMPTY;

            solver = dup_game(state);
            scratch = unruly_new_scratch(state);

            unruly_solve_game(solver, scratch, params->diff);

            if (unruly_validate_counts(solver, scratch, NULL) != 0)
                state->grid[i] = c;

            free_game(solver);
            unruly_free_scratch(scratch);
        }
        sfree(spaces);

#ifdef STANDALONE_SOLVER
        if (temp_verbose) {
            solver_verbose = TRUE;

            printf("Final puzzle:\n");
            debug = game_text_format(state);
            fputs(debug, stdout);
            sfree(debug);
        }
#endif

        /*
         * See if the game has accidentally come out too easy.
         */
        if (params->diff > 0) {
            int ok;
            game_state *solver;

            solver = dup_game(state);
            scratch = unruly_new_scratch(state);

            unruly_solve_game(solver, scratch, params->diff - 1);

            ok = unruly_validate_counts(solver, scratch, NULL);

            free_game(solver);
            unruly_free_scratch(scratch);

            if (ok)
                break;
        } else {
            /*
             * Puzzles of the easiest difficulty can't be too easy.
             */
            break;
        }
    }

    /* Encode description */
    ret = snewn(s + 1, char);
    p = ret;
    run = 0;
    for (i = 0; i < s+1; i++) {
        if (i == s || state->grid[i] == N_ZERO) {
            while (run > 24) {
                *p++ = 'z';
                run -= 25;
            }
            *p++ = 'a' + run;
            run = 0;
        } else if (state->grid[i] == N_ONE) {
            while (run > 24) {
                *p++ = 'Z';
                run -= 25;
            }
            *p++ = 'A' + run;
            run = 0;
        } else {
            run++;
        }
    }
    *p = '\0';

    free_game(state);

    return ret;
}

/* ************** *
 * User Interface *
 * ************** */

struct game_ui {
    int cx, cy;
    char cursor;
};

static game_ui *new_ui(game_state *state)
{
    game_ui *ret = snew(game_ui);

    ret->cx = ret->cy = 0;
    ret->cursor = FALSE;

    return ret;
}

static void free_ui(game_ui *ui)
{
    sfree(ui);
}

static char *encode_ui(game_ui *ui)
{
    return NULL;
}

static void decode_ui(game_ui *ui, char *encoding)
{
}

static void game_changed_state(game_ui *ui, game_state *oldstate,
                               game_state *newstate)
{
}

struct game_drawstate {
    int tilesize;
    int w2, h2;
    int started;

    int *gridfs;
    int *rowfs;

    int *grid;
};

static game_drawstate *game_new_drawstate(drawing *dr, game_state *state)
{
    struct game_drawstate *ds = snew(struct game_drawstate);

    int w2 = state->w2, h2 = state->h2;
    int s = w2 * h2;
    int i;

    ds->tilesize = 0;
    ds->w2 = w2;
    ds->h2 = h2;
    ds->started = FALSE;

    ds->gridfs = snewn(s, int);
    ds->rowfs = snewn(2 * (w2 + h2), int);

    ds->grid = snewn(s, int);
    for (i = 0; i < s; i++)
        ds->grid[i] = -1;

    return ds;
}

static void game_free_drawstate(drawing *dr, game_drawstate *ds)
{
    sfree(ds->gridfs);
    sfree(ds->rowfs);
    sfree(ds->grid);
    sfree(ds);
}

#define COORD(x)     ( (x) * ds->tilesize + ds->tilesize/2 )
#define FROMCOORD(x) ( ((x)-(ds->tilesize/2)) / ds->tilesize )

static char *interpret_move(game_state *state, game_ui *ui,
                            const game_drawstate *ds, int ox, int oy,
                            int button)
{
    int hx = ui->cx;
    int hy = ui->cy;

    int gx = FROMCOORD(ox);
    int gy = FROMCOORD(oy);

    int w2 = state->w2, h2 = state->h2;

    button &= ~MOD_MASK;

    /* Mouse click */
    if (button == LEFT_BUTTON || button == RIGHT_BUTTON ||
        button == MIDDLE_BUTTON) {
        if (ox >= (ds->tilesize / 2) && gx < w2
            && oy >= (ds->tilesize / 2) && gy < h2) {
            hx = gx;
            hy = gy;
            ui->cursor = FALSE;
        } else
            return NULL;
    }

    /* Keyboard move */
    if (IS_CURSOR_MOVE(button)) {
        move_cursor(button, &ui->cx, &ui->cy, w2, h2, 0);
        ui->cursor = TRUE;
        return "";
    }

    /* Place one */
    if ((ui->cursor && (button == CURSOR_SELECT || button == CURSOR_SELECT2
                        || button == '\b' || button == '0' || button == '1'
                        || button == '2')) ||
        button == LEFT_BUTTON || button == RIGHT_BUTTON ||
        button == MIDDLE_BUTTON) {
        char buf[80];
        char c, i;

        if (state->immutable[hy * w2 + hx])
            return NULL;

        c = '-';
        i = state->grid[hy * w2 + hx];

        if (button == '0' || button == '2')
            c = '0';
        else if (button == '1')
            c = '1';
        else if (button == MIDDLE_BUTTON)
            c = '-';

        /* Cycle through options */
        else if (button == CURSOR_SELECT || button == RIGHT_BUTTON)
            c = (i == EMPTY ? '0' : i == N_ZERO ? '1' : '-');
        else if (button == CURSOR_SELECT2 || button == LEFT_BUTTON)
            c = (i == EMPTY ? '1' : i == N_ONE ? '0' : '-');

        if (state->grid[hy * w2 + hx] ==
            (c == '0' ? N_ZERO : c == '1' ? N_ONE : EMPTY))
            return NULL;               /* don't put no-ops on the undo chain */

        sprintf(buf, "P%c,%d,%d", c, hx, hy);

        return dupstr(buf);
    }
    return NULL;
}

static game_state *execute_move(game_state *state, char *move)
{
    int w2 = state->w2, h2 = state->h2;
    int s = w2 * h2;
    int x, y, i;
    char c;

    game_state *ret;

    if (move[0] == 'S') {
        char *p;

        ret = dup_game(state);
        p = move + 1;

        for (i = 0; i < s; i++) {

            if (!*p || !(*p == '1' || *p == '0')) {
                free_game(ret);
                return NULL;
            }

            ret->grid[i] = (*p == '1' ? N_ONE : N_ZERO);
            p++;
        }

        ret->completed = ret->cheated = TRUE;
        return ret;
    } else if (move[0] == 'P'
               && sscanf(move + 1, "%c,%d,%d", &c, &x, &y) == 3 && x >= 0
               && x < w2 && y >= 0 && y < h2 && (c == '-' || c == '0'
                                                 || c == '1')) {
        ret = dup_game(state);
        i = y * w2 + x;

        if (state->immutable[i]) {
            free_game(ret);
            return NULL;
        }

        ret->grid[i] = (c == '1' ? N_ONE : c == '0' ? N_ZERO : EMPTY);

        if (!ret->completed && unruly_validate_counts(ret, NULL, NULL) == 0
            && (unruly_validate_all_rows(ret, NULL) == 0))
            ret->completed = TRUE;

        return ret;
    }

    return NULL;
}

/* ----------------------------------------------------------------------
 * Drawing routines.
 */

static void game_compute_size(game_params *params, int tilesize,
                              int *x, int *y)
{
    *x = tilesize * (params->w2 + 1);
    *y = tilesize * (params->h2 + 1);
}

static void game_set_size(drawing *dr, game_drawstate *ds,
                          game_params *params, int tilesize)
{
    ds->tilesize = tilesize;
}

static float *game_colours(frontend *fe, int *ncolours)
{
    float *ret = snewn(3 * NCOLOURS, float);
    int i;

    frontend_default_colour(fe, &ret[COL_BACKGROUND * 3]);

    for (i = 0; i < 3; i++) {
        ret[COL_1 * 3 + i] = 0.2F;
        ret[COL_1_HIGHLIGHT * 3 + i] = 0.4F;
        ret[COL_1_LOWLIGHT * 3 + i] = 0.0F;
        ret[COL_0 * 3 + i] = 0.95F;
        ret[COL_0_HIGHLIGHT * 3 + i] = 1.0F;
        ret[COL_0_LOWLIGHT * 3 + i] = 0.9F;
        ret[COL_EMPTY * 3 + i] = 0.5F;
        ret[COL_GRID * 3 + i] = 0.3F;
    }
    game_mkhighlight_specific(fe, ret, COL_0, COL_0_HIGHLIGHT, COL_0_LOWLIGHT);
    game_mkhighlight_specific(fe, ret, COL_1, COL_1_HIGHLIGHT, COL_1_LOWLIGHT);

    ret[COL_ERROR * 3 + 0] = 1.0F;
    ret[COL_ERROR * 3 + 1] = 0.0F;
    ret[COL_ERROR * 3 + 2] = 0.0F;

    ret[COL_CURSOR * 3 + 0] = 0.0F;
    ret[COL_CURSOR * 3 + 1] = 0.7F;
    ret[COL_CURSOR * 3 + 2] = 0.0F;

    *ncolours = NCOLOURS;
    return ret;
}

static void unruly_draw_err_rectangle(drawing *dr, int x, int y, int w, int h,
                                      int tilesize)
{
    double thick = tilesize / 10;
    double margin = tilesize / 20;

    draw_rect(dr, x+margin, y+margin, w-2*margin, thick, COL_ERROR);
    draw_rect(dr, x+margin, y+margin, thick, h-2*margin, COL_ERROR);
    draw_rect(dr, x+margin, y+h-margin-thick, w-2*margin, thick, COL_ERROR);
    draw_rect(dr, x+w-margin-thick, y+margin, thick, h-2*margin, COL_ERROR);
}

static void unruly_draw_tile(drawing *dr, int x, int y, int tilesize, int tile)
{
    clip(dr, x, y, tilesize, tilesize);

    /* Draw the grid edge first, so the tile can overwrite it */
    draw_rect(dr, x, y, tilesize, tilesize, COL_GRID);

    /* Background of the tile */
    {
        int val = (tile & FF_ZERO ? 0 : tile & FF_ONE ? 2 : 1);
        val = (val == 0 ? COL_0 : val == 2 ? COL_1 : COL_EMPTY);

        if ((tile & (FF_FLASH1 | FF_FLASH2)) &&
             (val == COL_0 || val == COL_1)) {
            val += (tile & FF_FLASH1 ? 1 : 2);
        }

        draw_rect(dr, x, y, tilesize-1, tilesize-1, val);

        if ((val == COL_0 || val == COL_1) && (tile & FF_IMMUTABLE)) {
            draw_rect(dr, x + tilesize/6, y + tilesize/6,
                      tilesize - 2*(tilesize/6) - 2, 1, val + 2);
            draw_rect(dr, x + tilesize/6, y + tilesize/6,
                      1, tilesize - 2*(tilesize/6) - 2, val + 2);
            draw_rect(dr, x + tilesize/6 + 1, y + tilesize - tilesize/6 - 2,
                      tilesize - 2*(tilesize/6) - 2, 1, val + 1);
            draw_rect(dr, x + tilesize - tilesize/6 - 2, y + tilesize/6 + 1,
                      1, tilesize - 2*(tilesize/6) - 2, val + 1);
        }
    }

    /* 3-in-a-row errors */
    if (tile & (FE_HOR_ROW_LEFT | FE_HOR_ROW_RIGHT)) {
        int left = x, right = x + tilesize - 1;
        if ((tile & FE_HOR_ROW_LEFT))
            right += tilesize/2;
        if ((tile & FE_HOR_ROW_RIGHT))
            left -= tilesize/2;
        unruly_draw_err_rectangle(dr, left, y, right-left, tilesize-1, tilesize);
    }
    if (tile & (FE_VER_ROW_TOP | FE_VER_ROW_BOTTOM)) {
        int top = y, bottom = y + tilesize - 1;
        if ((tile & FE_VER_ROW_TOP))
            bottom += tilesize/2;
        if ((tile & FE_VER_ROW_BOTTOM))
            top -= tilesize/2;
        unruly_draw_err_rectangle(dr, x, top, tilesize-1, bottom-top, tilesize);
    }

    /* Count errors */
    if (tile & FE_COUNT) {
        draw_text(dr, x + tilesize/2, y + tilesize/2, FONT_VARIABLE,
                  tilesize/2, ALIGN_HCENTRE | ALIGN_VCENTRE, COL_ERROR, "!");
    }

    /* Cursor rectangle */
    if (tile & FF_CURSOR) {
        draw_rect(dr, x, y, tilesize/12, tilesize-1, COL_CURSOR);
        draw_rect(dr, x, y, tilesize-1, tilesize/12, COL_CURSOR);
        draw_rect(dr, x+tilesize-1-tilesize/12, y, tilesize/12, tilesize-1,
                  COL_CURSOR);
        draw_rect(dr, x, y+tilesize-1-tilesize/12, tilesize-1, tilesize/12,
                  COL_CURSOR);
    }

    unclip(dr);
    draw_update(dr, x, y, tilesize, tilesize);
}

#define TILE_SIZE (ds->tilesize)
#define DEFAULT_TILE_SIZE 32
#define FLASH_FRAME 0.12F
#define FLASH_TIME (FLASH_FRAME * 3)

static void game_redraw(drawing *dr, game_drawstate *ds,
                        game_state *oldstate, game_state *state, int dir,
                        game_ui *ui, float animtime, float flashtime)
{
    int w2 = state->w2, h2 = state->h2;
    int s = w2 * h2;
    int flash;
    int x, y, i;

    if (!ds->started) {
        /* Main window background */
        draw_rect(dr, 0, 0, TILE_SIZE * (w2+1), TILE_SIZE * (h2+1),
                  COL_BACKGROUND);
        /* Outer edge of grid */
        draw_rect(dr, COORD(0)-TILE_SIZE/10, COORD(0)-TILE_SIZE/10,
                  TILE_SIZE*w2 + 2*(TILE_SIZE/10) - 1,
                  TILE_SIZE*h2 + 2*(TILE_SIZE/10) - 1, COL_GRID);

        draw_update(dr, 0, 0, TILE_SIZE * (w2+1), TILE_SIZE * (h2+1));
        ds->started = TRUE;
    }

    flash = 0;
    if (flashtime > 0)
        flash = (int)(flashtime / FLASH_FRAME) == 1 ? FF_FLASH2 : FF_FLASH1;

    for (i = 0; i < s; i++)
        ds->gridfs[i] = 0;
    unruly_validate_all_rows(state, ds->gridfs);
    for (i = 0; i < 2 * (h2 + w2); i++)
        ds->rowfs[i] = 0;
    unruly_validate_counts(state, NULL, ds->rowfs);

    for (y = 0; y < h2; y++) {
        for (x = 0; x < w2; x++) {
            int tile;

            i = y * w2 + x;

            tile = ds->gridfs[i];

            if (state->grid[i] == N_ONE) {
                tile |= FF_ONE;
                if (ds->rowfs[y] || ds->rowfs[2*h2 + x])
                    tile |= FE_COUNT;
            } else if (state->grid[i] == N_ZERO) {
                tile |= FF_ZERO;
                if (ds->rowfs[h2 + y] || ds->rowfs[2*h2 + w2 + x])
                    tile |= FE_COUNT;
            }

            tile |= flash;

            if (state->immutable[i])
                tile |= FF_IMMUTABLE;

            if (ui->cursor && ui->cx == x && ui->cy == y)
                tile |= FF_CURSOR;

            if (ds->grid[i] != tile) {
                ds->grid[i] = tile;
                unruly_draw_tile(dr, COORD(x), COORD(y), TILE_SIZE, tile);
            }
        }
    }
}

static float game_anim_length(game_state *oldstate, game_state *newstate,
                              int dir, game_ui *ui)
{
    return 0.0F;
}

static float game_flash_length(game_state *oldstate,
                               game_state *newstate, int dir,
                               game_ui *ui)
{
    if (!oldstate->completed && newstate->completed &&
        !oldstate->cheated && !newstate->cheated)
        return FLASH_TIME;
    return 0.0F;
}

static int game_status(game_state *state)
{
    return state->completed ? +1 : 0;
}

static int game_timing_state(game_state *state, game_ui *ui)
{
    return TRUE;
}

static void game_print_size(game_params *params, float *x, float *y)
{
    int pw, ph;

    /* Using 7mm squares */
    game_compute_size(params, 700, &pw, &ph);
    *x = pw / 100.0F;
    *y = ph / 100.0F;
}

static void game_print(drawing *dr, game_state *state, int tilesize)
{
    int w2 = state->w2, h2 = state->h2;
    int x, y;

    int ink = print_mono_colour(dr, 0);

    for (y = 0; y < h2; y++)
        for (x = 0; x < w2; x++) {
            int tx = x * tilesize + (tilesize / 2);
            int ty = y * tilesize + (tilesize / 2);

            /* Draw the border */
            int coords[8];
            coords[0] = tx;
            coords[1] = ty - 1;
            coords[2] = tx + tilesize;
            coords[3] = ty - 1;
            coords[4] = tx + tilesize;
            coords[5] = ty + tilesize - 1;
            coords[6] = tx;
            coords[7] = ty + tilesize - 1;
            draw_polygon(dr, coords, 4, -1, ink);

            if (state->grid[y * w2 + x] == N_ONE)
                draw_rect(dr, tx, ty, tilesize, tilesize, ink);
            else if (state->grid[y * w2 + x] == N_ZERO)
                draw_circle(dr, tx + tilesize/2, ty + tilesize/2,
                            tilesize/12, ink, ink);
        }
}

#ifdef COMBINED
#define thegame unruly
#endif

const struct game thegame = {
    "Unruly", "games.unruly", "unruly",
    default_params,
    game_fetch_preset,
    decode_params,
    encode_params,
    free_params,
    dup_params,
    TRUE, game_configure, custom_params,
    validate_params,
    new_game_desc,
    validate_desc,
    new_game,
    dup_game,
    free_game,
    TRUE, solve_game,
    TRUE, game_can_format_as_text_now, game_text_format,
    new_ui,
    free_ui,
    encode_ui,
    decode_ui,
    game_changed_state,
    interpret_move,
    execute_move,
    DEFAULT_TILE_SIZE, game_compute_size, game_set_size,
    game_colours,
    game_new_drawstate,
    game_free_drawstate,
    game_redraw,
    game_anim_length,
    game_flash_length,
    game_status,
    TRUE, FALSE, game_print_size, game_print,
    FALSE,                      /* wants_statusbar */
    FALSE, game_timing_state,
    0,                          /* flags */
};

/* ***************** *
 * Standalone solver *
 * ***************** */

#ifdef STANDALONE_SOLVER
#include <time.h>
#include <stdarg.h>

/* Most of the standalone solver code was copied from unequal.c and singles.c */

const char *quis;

static void usage_exit(const char *msg)
{
    if (msg)
        fprintf(stderr, "%s: %s\n", quis, msg);
    fprintf(stderr,
            "Usage: %s [-v] [--seed SEED] <params> | [game_id [game_id ...]]\n",
            quis);
    exit(1);
}

int main(int argc, char *argv[])
{
    random_state *rs;
    time_t seed = time(NULL);

    game_params *params = NULL;

    char *id = NULL, *desc = NULL, *err;

    quis = argv[0];

    while (--argc > 0) {
        char *p = *++argv;
        if (!strcmp(p, "--seed")) {
            if (argc == 0)
                usage_exit("--seed needs an argument");
            seed = (time_t) atoi(*++argv);
            argc--;
        } else if (!strcmp(p, "-v"))
            solver_verbose = TRUE;
        else if (*p == '-')
            usage_exit("unrecognised option");
        else
            id = p;
    }

    if (id) {
        desc = strchr(id, ':');
        if (desc)
            *desc++ = '\0';

        params = default_params();
        decode_params(params, id);
        err = validate_params(params, TRUE);
        if (err) {
            fprintf(stderr, "Parameters are invalid\n");
            fprintf(stderr, "%s: %s", argv[0], err);
            exit(1);
        }
    }

    if (!desc) {
        char *desc_gen, *aux;
        rs = random_new((void *) &seed, sizeof(time_t));
        if (!params)
            params = default_params();
        printf("Generating puzzle with parameters %s\n",
               encode_params(params, TRUE));
        desc_gen = new_game_desc(params, rs, &aux, FALSE);

        if (!solver_verbose) {
            char *fmt = game_text_format(new_game(NULL, params, desc_gen));
            fputs(fmt, stdout);
            sfree(fmt);
        }

        printf("Game ID: %s\n", desc_gen);
    } else {
        game_state *input;
        struct unruly_scratch *scratch;
        int maxdiff, errcode;

        err = validate_desc(params, desc);
        if (err) {
            fprintf(stderr, "Description is invalid\n");
            fprintf(stderr, "%s", err);
            exit(1);
        }

        input = new_game(NULL, params, desc);
        scratch = unruly_new_scratch(input);

        maxdiff = unruly_solve_game(input, scratch, DIFFCOUNT);

        errcode = unruly_validate_counts(input, scratch, NULL);
        if (unruly_validate_all_rows(input, NULL) == -1)
            errcode = -1;

        if (errcode != -1) {
            char *fmt = game_text_format(input);
            fputs(fmt, stdout);
            sfree(fmt);
            if (maxdiff < 0)
                printf("Difficulty: already solved!\n");
            else
                printf("Difficulty: %s\n", unruly_diffnames[maxdiff]);
        }

        if (errcode == 1)
            printf("No solution found.\n");
        else if (errcode == -1)
            printf("Puzzle is invalid.\n");

        free_game(input);
        unruly_free_scratch(scratch);
    }

    return 0;
}
#endif
