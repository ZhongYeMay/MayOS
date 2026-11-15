#!/usr/bin/env python3
"""Host-side tests for the actual freestanding MayDOS math and Minesweeper C."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TEST = r'''
#include "maydos_tools.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char out[64];
    assert(md_calc_eval("1+2*3", out, sizeof(out)) && !strcmp(out,"7"));
    assert(md_calc_eval("(1.5+2.5)/2", out, sizeof(out)) && !strcmp(out,"2"));
    assert(md_calc_eval(" -4 + 2.25", out, sizeof(out)) && !strcmp(out,"-1.75"));
    assert(md_calc_eval("-.5*2", out, sizeof(out)) && !strcmp(out,"-1"));
    assert(!md_calc_eval("10/0", out, sizeof(out)) && !strcmp(out,"Division by zero"));
    assert(!md_calc_eval("1+abc", out, sizeof(out)));
    assert(!md_calc_eval("((1+2)", out, sizeof(out)));
    assert(!md_calc_eval("10000000*2", out, sizeof(out)));
    char encoded[64], decoded[64];
    assert(md_note_encode("Hello\\n", 6, encoded, sizeof(encoded)) == 8);
    assert(!strcmp(encoded, "SGVsbG8K"));
    assert(md_note_decode(encoded, 8, decoded, sizeof(decoded)) == 6);
    assert(!strcmp(decoded, "Hello\\n"));
    assert(md_note_decode("!!??", 4, decoded, sizeof(decoded)) < 0);
    MayDosMines g;
    assert(md_mines_new(&g,1,0,0,0,123));
    assert(g.rows==9 && g.cols==9 && g.mines==10);
    assert(md_mines_reveal(&g,0,0,900)>0 && g.values[0]!=9);
    int total=0;
    for(int i=0;i<g.rows*g.cols;i++) total+=(g.values[i]==9);
    assert(total==10);
    /* Every displayed count matches the actual surrounding bombs. */
    for(int r=0;r<g.rows;r++) for(int c=0;c<g.cols;c++) {
        if(g.values[r*g.cols+c]==9) continue;
        int adjacent=0;
        for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
            int nr=r+dy,nc=c+dx;
            if(nr>=0 && nr<g.rows && nc>=0 && nc<g.cols &&
               g.values[nr*g.cols+nc]==9) adjacent++;
        }
        assert(g.values[r*g.cols+c]==adjacent);
    }
    assert(md_mines_new(&g,2,0,0,0,123) && g.mines==40);
    assert(md_mines_new(&g,3,0,0,0,123) && g.mines==99 && g.cols==30);
    assert(!md_mines_new(&g,4,10,10,2,2));
    assert(md_mines_new(&g,4,10,10,20,2));
    assert(md_mines_flag(&g,3,3)==1);
    assert(g.flags==1 && g.states[3*g.cols+3]==2);
    assert(md_mines_flag(&g,3,3)==1 && g.flags==0);
    puts("PASS: native MayDOS calculator parser, safe first move, mine counts, modes and flags");
    return 0;
}
'''
def main():
    with tempfile.TemporaryDirectory(prefix="maydos-test-") as td:
        test = Path(td) / "test.c"
        exe = Path(td) / "maydos-test"
        test.write_text(TEST)
        subprocess.run(["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "kernel"),
                        str(ROOT / "kernel/maydos_tools.c"), str(test), "-o", str(exe)],
                       check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
