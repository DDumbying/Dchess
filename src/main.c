#include <stdio.h>
#include "engine/board.h"

int main() {
    Position pos;

    init_start_position(&pos);
    print_board(&pos);

    printf("White occupancy: %llx\n", pos.occupancies[0]);
    printf("Black occupancy: %llx\n", pos.occupancies[1]);

    return 0;
}
