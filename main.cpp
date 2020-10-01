#include <stdio.h>
#include <stdlib.h>
#include <limits>
#include <string.h>
#include <time.h>
#include <stdint.h>

static constexpr auto SEARCH_DEPTH = 8;

static clock_t clock_begin_path;
static uint64_t boards_evaluated;

inline uint64_t absolute_value(int value) {
	if (value < 0)
		return -value;
	return value;
}

enum Team
{
	WHITE = 0,
	BLACK = 1
};

enum PieceType
{
	KING = 2,
	QUEEN = 4,
	ROOK = 8,
	BISHOP = 16,
	KNIGHT = 32,
	PAWN = 64,
	NONE = 128
};

const uint8_t Type_Mask = KING | QUEEN | ROOK | BISHOP | KNIGHT | PAWN;

struct Piece
{
	uint8_t info;
	Piece() = default;
	Piece(uint8_t _info) : info(_info) {}
	Team team() const { return (Team)(info & 1); }
	Team other_team() const { return team() == Team::WHITE ? Team::BLACK : Team::WHITE; }
	PieceType type() const { return (PieceType)(info & ~Team::BLACK); }
};

enum MoveFlags
{
	NO_ACTION = 0,
	ATTACK = 1,
	// Possible attacks of:
	// KING = 2,
	// QUEEN = 4,
	// ROOK = 8,
	// BISHOP = 16,
	// KNIGHT = 32,
	// PAWN = 64,
	FIRST_MOVE = 128,
	CASTLE_LEFT_BEFORE_MOVE = 256,
	CASTLE_RIGHT_BEFORE_MOVE = 512,
	PROMOTION = 1024,
	DOUBLE_MOVE = 2048,
	EN_PASSANT = 4096,
};

struct Move
{
	uint8_t source{};
	uint8_t destination{};
	MoveFlags flags{};

	PieceType get_attacked_piece_type() { return (PieceType)(flags & Type_Mask); }
};

void print_move(Move move) {
	printf("%c%c %c%c\n", (char)('a' + move.source / 8), (char)('1' + move.source % 8),
		(char)('a' + move.destination / 8), (char)('1' + move.destination % 8));
}

enum GameFlags
{
	CAN_WHITE_CASTLE_RIGHT = 1,
	CAN_WHITE_CASTLE_LEFT = 2,
	CAN_BLACK_CASTLE_RIGHT = 4,
	CAN_BLACK_CASTLE_LEFT = 8
};

struct MoveHistory
{
	Move moves[512];
	int cursor = 0;
	inline void add(Move to_add) { moves[cursor++] = to_add; }
	inline Move pop() {
		return moves[--cursor];
	}
	inline Move peek() { return moves[cursor - 1]; }
};

struct ChessGame
{
	bool is_against_ai;
	GameFlags flags{};
	Team current_turn;
	Piece board[8 * 8];
	MoveHistory history;
	inline Piece piece_at(int8_t col, int8_t row) { return board[row * 8 + col]; }
	inline bool can_castle_right(bool is_white) { return is_white ? flags & GameFlags::CAN_WHITE_CASTLE_RIGHT : flags & GameFlags::CAN_BLACK_CASTLE_RIGHT; }
	inline void set_castle_right(bool value, bool is_white) {
		if (is_white)
			if (value)
				flags = (GameFlags)(flags | GameFlags::CAN_WHITE_CASTLE_RIGHT);
			else
				flags = (GameFlags)(flags & ~GameFlags::CAN_WHITE_CASTLE_RIGHT);
		else
			if (value)
				flags = (GameFlags)(flags | GameFlags::CAN_BLACK_CASTLE_RIGHT);
			else
				flags = (GameFlags)(flags & ~GameFlags::CAN_BLACK_CASTLE_RIGHT);
	}
	inline bool can_castle_left(bool is_white) { return is_white ? flags & GameFlags::CAN_WHITE_CASTLE_LEFT : flags & GameFlags::CAN_BLACK_CASTLE_LEFT; }
	inline void set_castle_left(bool value, bool is_white) {
		if (is_white)
			if (value)
				flags = (GameFlags)(flags | GameFlags::CAN_WHITE_CASTLE_LEFT);
			else
				flags = (GameFlags)(flags & ~GameFlags::CAN_WHITE_CASTLE_LEFT);
		else
			if (value)
				flags = (GameFlags)(flags | GameFlags::CAN_BLACK_CASTLE_LEFT);
			else
				flags = (GameFlags)(flags & ~GameFlags::CAN_BLACK_CASTLE_LEFT);
	}
};

static const auto INVALID_POSITION = (uint8_t)-1;

uint8_t pieces_on_board_count(ChessGame* game) {
	uint8_t result = 0;
	for (uint8_t i = 0; i < 8 * 8; ++i)
		if (game->board[i].type() != PieceType::NONE)
			++result;
	return result;
}

uint8_t NOT_FOUND = (uint8_t)-1;
uint8_t index_of_king(ChessGame* game, Team team) {
	for (uint8_t i = 0; i < 8 * 8; ++i)
		if (game->board[i].type() == PieceType::KING
			&& game->board[i].team() == team)
			return i;
	return NOT_FOUND;
}

template<typename Callback>
bool foreach_piece_legal_move(ChessGame* game, uint8_t piece_position, Callback callback, bool full_check = true);

template<typename Callback>
bool foreach_team_legal_move(ChessGame* game, Team team, Callback callback, bool full_check = false);

bool check_move_full_legality(ChessGame* game, Move move);

void performe_move(ChessGame* game, Move move) {
	game->current_turn = (Team)(game->current_turn ^ Team::BLACK);

	game->history.add(move);

	Piece piece = game->board[move.source];
	bool is_white = piece.team() == Team::WHITE;

	game->board[move.destination] = piece;
	game->board[move.source] = Piece(PieceType::NONE);

	if (piece.type() == PieceType::PAWN) {
		if (move.flags & MoveFlags::EN_PASSANT) {
			int8_t offset = absolute_value(move.source - move.destination) == 9 ? 1 : -1;
			if (is_white)
				offset = -offset;
			game->board[move.source + offset] = PieceType::NONE;
		} else if (move.flags & MoveFlags::PROMOTION)
			game->board[move.destination] = Piece(PieceType::QUEEN | piece.team());
	} else if (piece.type() == PieceType::KING) {
		uint8_t source_col = move.source % 8;
		uint8_t dest_col = move.destination % 8;
		if (absolute_value(source_col - dest_col) > 1) {
			uint8_t row = move.destination / 8;
			if (dest_col == 6) {
				game->board[row * 8 + 5] = game->board[row * 8 + 7];
				game->board[row * 8 + 7] = PieceType::NONE;
			} else {
				game->board[row * 8 + dest_col + 1] = game->board[row * 8];
				game->board[row * 8] = PieceType::NONE;
			}
		}
	}
	if (move.flags & MoveFlags::FIRST_MOVE) {
		if (piece.type() == PieceType::KING) {
			game->set_castle_right(false, is_white);
			game->set_castle_left(false, is_white);
		} else if (piece.type() == PieceType::ROOK) {
			uint8_t col = move.source % 8;
			if (col == 7)
				game->set_castle_right(false, is_white);
			else
				game->set_castle_left(false, is_white);
		}
	}
}

void undo_last_move(ChessGame* game) {
	game->current_turn = (Team)(game->current_turn ^ Team::BLACK);

	Move move = game->history.pop();

	Piece piece = game->board[move.destination];
	bool is_white = piece.team() == Team::WHITE;

	game->board[move.source] = piece;
	game->board[move.destination] = Piece(PieceType::NONE);

	if (move.flags & MoveFlags::ATTACK)
		game->board[move.destination] = Piece(move.get_attacked_piece_type() | piece.other_team());
	if (move.flags & MoveFlags::PROMOTION)
		game->board[move.source] = Piece(PieceType::PAWN | piece.team());

	if (piece.type() == PieceType::PAWN) {
		if (move.flags & MoveFlags::EN_PASSANT) {
			int8_t offset = absolute_value(move.source - move.destination) == 9 ? 1 : -1;
			if (is_white)
				offset = -offset;
			game->board[move.source + offset] = Piece(PieceType::PAWN | piece.other_team());
		}
	} else if (piece.type() == PieceType::KING) {
		uint8_t source_col = move.source % 8;
		uint8_t dest_col = move.destination % 8;
		if (absolute_value(source_col - dest_col) > 1) {
			uint8_t row = move.source / 8;
			if (dest_col == 6) {
				game->board[row * 8 + 7] = Piece(PieceType::ROOK | piece.team());
				game->board[row * 8 + 5] = PieceType::NONE;
			} else {
				game->board[row * 8] = game->board[row * 8 + dest_col + 1];
				game->board[row * 8 + dest_col + 1] = PieceType::NONE;
			}
		}
	}

	if (piece.type() == PieceType::KING || piece.type() == PieceType::ROOK)
		if (move.flags & MoveFlags::FIRST_MOVE) {
			if (move.flags & MoveFlags::CASTLE_RIGHT_BEFORE_MOVE)
				game->set_castle_right(true, is_white);

			if (move.flags & MoveFlags::CASTLE_LEFT_BEFORE_MOVE)
				game->set_castle_left(true, is_white);
		}
}

enum IterationStatus
{
	BREAK,
	CONTINUE
};

bool any_legal_destinations_for_team(ChessGame* game, Team team, int8_t* destinations_to_check, int num_destinations_to_check) {
	bool result = false;
	foreach_team_legal_move(game, team,
		[num_destinations_to_check, destinations_to_check, &result](Move move)
	{
		for (int i = 0; i < num_destinations_to_check; ++i) {
			if (move.destination == destinations_to_check[i]) {
				result = true;
				return IterationStatus::BREAK;
			}
		}
		return IterationStatus::CONTINUE;
	},
		false);
	return result;
}

bool check_move_full_legality(ChessGame* game, Move move) {
	Team other_team = game->board[move.source].other_team();
	performe_move(game, move);
	bool result = true;
	foreach_team_legal_move(game, other_team,
		[&result](Move move)
	{
		if (move.flags & MoveFlags::ATTACK
			&& move.get_attacked_piece_type() == PieceType::KING) {
			result = false;
			return IterationStatus::BREAK;
		}
		return IterationStatus::CONTINUE;
	},
		false);
	undo_last_move(game);
	return result;
}

#define Call_On(dest_col, dest_row, flags)															\
	{																																			\
		Move move = Move{ (uint8_t)piece_position, (uint8_t)((dest_row) * 8 + (dest_col)), (MoveFlags)(flags) }; \
		if((!full_check) || check_move_full_legality(game, move))						\
			if(callback(move) == IterationStatus::BREAK)											\
				return true;																										\
	}																																			\

#define Call_On_And_Maybe_Attack(col, row)										\
	{																														\
		if(col >= 0 && col < 8 && row >= 0 && row < 8) {					\
			Piece other = game->piece_at(col, row);									\
			if(other.type() == PieceType::NONE)	{										\
				Call_On(col, row, MoveFlags::NO_ACTION);							\
			}																												\
			else if(other.team() != piece.team())	{									\
				Call_On(col, row, MoveFlags::ATTACK | other.type());	\
			}																												\
		}																													\
	}																														\

#define Iterate_Col_And_Row(col_dir, row_dir, flags)										\
	for(int8_t c = col + (col_dir), r = row + (row_dir); c < 8 && r < 8 && c > -1 && r > -1; c += (col_dir), r += (row_dir)) \
	{																																			\
		Piece other = game->piece_at(c, r);																	\
		if(other.type() == PieceType::NONE)																	\
		{																																		\
			Call_On(c, r, flags);																							\
		}																																		\
		else																																\
		{																																		\
			if(other.team() != piece.team())																	\
			{																																	\
				Call_On(c, r, flags | MoveFlags::ATTACK | other.type());				\
			}																																	\
			break;																														\
		}																																		\
	}																																			

#define Is_Occupied(col, row) (game->board[(row) * 8 + (col)].type() != PieceType::NONE)


template<typename Callback>
bool foreach_piece_legal_move(ChessGame* game, uint8_t piece_position, Callback callback, bool full_check) {
	Piece piece = game->board[piece_position];
	bool is_white = piece.team() == Team::WHITE;
	int8_t col = piece_position % 8;
	int8_t row = piece_position / 8;
	switch (piece.type()) {
	case PieceType::PAWN: {
		int8_t direction = is_white ? -1 : 1;
		int8_t double_move_row = is_white ? 6 : 1;
		MoveFlags promotion_flag = (row + direction == 0 || row + direction == 7) ? MoveFlags::PROMOTION : MoveFlags::NO_ACTION;

		if (!Is_Occupied(col, row + direction)) {
			Call_On(col, row + direction, promotion_flag);
			if (row == double_move_row && !Is_Occupied(col, row + direction * 2))
				Call_On(col, row + direction * 2, MoveFlags::DOUBLE_MOVE);
		}

		if (col - 1 >= 0) {
			Piece left = game->piece_at(col - 1, row + direction);
			if (left.type() != PieceType::NONE && left.team() != piece.team())
				Call_On(col - 1, row + direction, promotion_flag | MoveFlags::ATTACK | left.type());
		}

		if (col + 1 <= 7) {
			Piece right = game->piece_at(col + 1, row + direction);
			if (right.type() != PieceType::NONE && right.team() != piece.team())
				Call_On(col + 1, row + direction, promotion_flag | MoveFlags::ATTACK | right.type());
		}
		if (game->history.cursor > 0) {
			uint8_t fifth_rank = is_white ? 3 : 4;
			Move last_move = game->history.peek();
			uint8_t last_move_row = last_move.destination / 8;
			uint8_t last_move_col = last_move.destination % 8;
			if (last_move.flags & MoveFlags::DOUBLE_MOVE && row == fifth_rank
				&& row == last_move_row && absolute_value(col - last_move_col) == 1) {
				Call_On(col + (last_move.destination - piece_position), row + direction, MoveFlags::EN_PASSANT);
			}
		}
	} break;

	case PieceType::KING: {
		MoveFlags first_move_flags = MoveFlags::NO_ACTION;

		if (game->can_castle_right(is_white))
			first_move_flags = (MoveFlags)(first_move_flags | MoveFlags::FIRST_MOVE | MoveFlags::CASTLE_RIGHT_BEFORE_MOVE);

		if (game->can_castle_left(is_white))
			first_move_flags = (MoveFlags)(first_move_flags | MoveFlags::FIRST_MOVE | MoveFlags::CASTLE_LEFT_BEFORE_MOVE);

		for (int8_t i = row - 1; i <= row + 1; ++i) {
			if (i < 0 || i > 7)
				continue;
			for (int8_t j = col - 1; j <= col + 1; ++j) {
				if (j < 0 || j > 7)
					continue;
				Piece other = game->piece_at(j, i);
				if (other.type() == PieceType::NONE) {
					Call_On(j, i, first_move_flags);
				} else if (other.team() != piece.team()) {
					Call_On(j, i, first_move_flags | MoveFlags::ATTACK | other.type());
				}
			}
		}
		if (full_check) {
			if (game->can_castle_right(is_white)) {
				Piece maybe_right_rook = game->piece_at(7, row);
				if (maybe_right_rook.type() == PieceType::ROOK && maybe_right_rook.team() == piece.team() && !Is_Occupied(5, row) && !Is_Occupied(6, row)) {
					int8_t destinations_to_check[3] = {
					  row * 8 + 4,
					  row * 8 + 5,
					  row * 8 + 6,
					};
					if (!any_legal_destinations_for_team(game, piece.other_team(), destinations_to_check, 3)) {
						Call_On(6, row, first_move_flags);
					}
				}
			}
			if (game->can_castle_left(is_white)) {
				Piece maybe_left_rook = game->piece_at(0, row);
				if (maybe_left_rook.type() == PieceType::ROOK && maybe_left_rook.team() == piece.team() && !Is_Occupied(3, row) && !Is_Occupied(2, row) && !Is_Occupied(1, row)) {
					int8_t destinations_to_check[4] = {
					  row * 8 + 1,
					  row * 8 + 2,
					  row * 8 + 3,
					  row * 8 + 4,
					};
					if (!any_legal_destinations_for_team(game, piece.other_team(), destinations_to_check, 4)) {
						Call_On(2, row, first_move_flags);
						Call_On(1, row, first_move_flags);
					}
				}
			}
		}
	} break;
	case PieceType::QUEEN: {
		Iterate_Col_And_Row(-1, -1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(1, 1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(1, -1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(-1, 1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(0, -1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(0, 1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(1, 0, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(-1, 0, MoveFlags::NO_ACTION);
	} break;

	case PieceType::BISHOP: {
		Iterate_Col_And_Row(-1, -1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(1, 1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(1, -1, MoveFlags::NO_ACTION);
		Iterate_Col_And_Row(-1, 1, MoveFlags::NO_ACTION);
	} break;

	case PieceType::KNIGHT: {
		Call_On_And_Maybe_Attack(col + 1, row + 2);
		Call_On_And_Maybe_Attack(col + 1, row - 2);
		Call_On_And_Maybe_Attack(col - 1, row + 2);
		Call_On_And_Maybe_Attack(col - 1, row - 2);
		Call_On_And_Maybe_Attack(col + 2, row + 1);
		Call_On_And_Maybe_Attack(col + 2, row - 1);
		Call_On_And_Maybe_Attack(col - 2, row + 1);
		Call_On_And_Maybe_Attack(col - 2, row - 1);
	} break;

	case PieceType::ROOK: {
		MoveFlags first_move_flag = MoveFlags::NO_ACTION;

		if (game->can_castle_right(is_white) && col == 7)
			first_move_flag = MoveFlags::FIRST_MOVE;
		if (game->can_castle_left(is_white) && col == 0)
			first_move_flag = MoveFlags::FIRST_MOVE;

		if (first_move_flag & MoveFlags::FIRST_MOVE) {
			if (game->can_castle_right(is_white))
				first_move_flag = (MoveFlags)(first_move_flag | MoveFlags::CASTLE_RIGHT_BEFORE_MOVE);
			if (game->can_castle_left(is_white))
				first_move_flag = (MoveFlags)(first_move_flag | MoveFlags::CASTLE_LEFT_BEFORE_MOVE);
		}

		Iterate_Col_And_Row(0, -1, first_move_flag);
		Iterate_Col_And_Row(0, 1, first_move_flag);
		Iterate_Col_And_Row(1, 0, first_move_flag);
		Iterate_Col_And_Row(-1, 0, first_move_flag);
	} break;
	}
	return false;
}

template<typename Callback>
bool foreach_team_legal_move(ChessGame* game, Team team, Callback callback, bool full_check) {
	for (int i = 0; i < 8 * 8; ++i) {
		Piece piece = game->board[i];
		if (piece.type() != PieceType::NONE && piece.team() == team) {
			if (foreach_piece_legal_move(game, i, callback, full_check))
				return true;
		}
	}
	return false;
}

void init_game(ChessGame* out_game) {
	while (true) {
		printf("Do you want to play against AI? (y | n): ");
		char answer = getchar();
		if (answer == 'n' || answer == 'y') {
			out_game->is_against_ai = answer == 'y';
			break;
		} else {
			printf("Invalid input!\n");
		}
	}

	out_game->flags = (GameFlags)
		(GameFlags::CAN_WHITE_CASTLE_RIGHT | GameFlags::CAN_WHITE_CASTLE_LEFT |
			GameFlags::CAN_BLACK_CASTLE_RIGHT | GameFlags::CAN_BLACK_CASTLE_LEFT);
	out_game->current_turn = Team::WHITE;

	memset(out_game->board, (uint8_t)PieceType::NONE, 8 * 8);

	out_game->board[0] = (PieceType::ROOK | Team::BLACK);
	out_game->board[1] = (PieceType::KNIGHT | Team::BLACK);
	out_game->board[2] = (PieceType::BISHOP | Team::BLACK);
	out_game->board[3] = (PieceType::QUEEN | Team::BLACK);
	out_game->board[4] = (PieceType::KING | Team::BLACK);
	out_game->board[5] = (PieceType::BISHOP | Team::BLACK);
	out_game->board[6] = (PieceType::KNIGHT | Team::BLACK);
	out_game->board[7] = (PieceType::ROOK | Team::BLACK);
	out_game->board[8] = (PieceType::PAWN | Team::BLACK);
	out_game->board[9] = (PieceType::PAWN | Team::BLACK);
	out_game->board[10] = (PieceType::PAWN | Team::BLACK);
	out_game->board[11] = (PieceType::PAWN | Team::BLACK);
	out_game->board[12] = (PieceType::PAWN | Team::BLACK);
	out_game->board[13] = (PieceType::PAWN | Team::BLACK);
	out_game->board[14] = (PieceType::PAWN | Team::BLACK);
	out_game->board[15] = (PieceType::PAWN | Team::BLACK);

	out_game->board[56] = (PieceType::ROOK | Team::WHITE);
	out_game->board[57] = (PieceType::KNIGHT | Team::WHITE);
	out_game->board[58] = (PieceType::BISHOP | Team::WHITE);
	out_game->board[59] = (PieceType::QUEEN | Team::WHITE);
	out_game->board[60] = (PieceType::KING | Team::WHITE);
	out_game->board[61] = (PieceType::BISHOP | Team::WHITE);
	out_game->board[62] = (PieceType::KNIGHT | Team::WHITE);
	out_game->board[63] = (PieceType::ROOK | Team::WHITE);
	out_game->board[48] = (PieceType::PAWN | Team::WHITE);
	out_game->board[49] = (PieceType::PAWN | Team::WHITE);
	out_game->board[50] = (PieceType::PAWN | Team::WHITE);
	out_game->board[51] = (PieceType::PAWN | Team::WHITE);
	out_game->board[52] = (PieceType::PAWN | Team::WHITE);
	out_game->board[53] = (PieceType::PAWN | Team::WHITE);
	out_game->board[54] = (PieceType::PAWN | Team::WHITE);
	out_game->board[55] = (PieceType::PAWN | Team::WHITE);
}

void print_board(ChessGame* game) {
	bool has_moved = game->history.cursor > 0;
	uint8_t last_src;
	uint8_t last_dst;
	if (has_moved) {
		Move last_move = game->history.peek();
		last_src = last_move.source;
		last_dst = last_move.destination;
	}

	printf("    1 2 3 4 5 6 7 8\n");
	for (int i = 0; i < 64; ++i) {
		if (i % 8 == 0) {
			printf("%c  ", 'a' + (i / 8));
		}

		const auto piece = game->board[i];

		if (piece.team() == Team::BLACK)
			printf("\u001b[30;1m");
		else
			printf("\u001b[31;1m");

		if (has_moved && (i == last_src || i == last_dst)) {
			if ((i + i / 8) % 2 != 0)
				printf("\x1b[106m");
			else
				printf("\x1b[46m");
		} else if ((i + i / 8) % 2 != 0) {
			printf("\x1b[107m");
		} else {
			printf("\x1b[100m");
		}

		char c;

		switch (piece.type()) {
		case PieceType::NONE: {
			c = ' ';
		} break;
		case PieceType::PAWN: {
			c = 'P';
		} break;
		case PieceType::KING: {
			c = 'K';
		} break;
		case PieceType::QUEEN: {
			c = 'Q';
		} break;
		case PieceType::BISHOP: {
			c = 'B';
		} break;
		case PieceType::KNIGHT: {
			c = 'G';
		} break;
		case PieceType::ROOK: {
			c = 'R';
		} break;
		}

		printf(" %c", c);

		printf("\033[0m"); // Color reset.

		if (i % 8 == 7)
			printf("\n");
	}
}

bool parse_move(char* str, Move* out) {
	enum Parse_State {
		SRC_ROW,
		SRC_COL,
		DST_ROW,
		DST_COL,
		DONE,
	};

	Parse_State parse_state = SRC_ROW;
	auto should_skip = [](char c) { return c == ' '; };
	auto is_legal_row = [](char c) { return c >= 'a' && c <= 'h'; };
	auto is_legal_col = [](char c) { return c >= '1' && c <= '8'; };

	for (char* it = str; *it != '\0'; ++it) {
		if (should_skip(*it))
			continue;

		switch (parse_state) {
		case SRC_ROW: {
			if (!is_legal_row(*it))
				return false;
			out->source = (*it - 'a') * 8;
			parse_state = SRC_COL;
		} break;

		case SRC_COL: {
			if (!is_legal_col(*it))
				return false;
			out->source += (*it - '1');
			parse_state = DST_ROW;
		} break;

		case DST_ROW: {
			if (!is_legal_row(*it))
				return false;
			out->destination = (*it - 'a') * 8;
			parse_state = DST_COL;
		} break;

		case DST_COL: {
			if (!is_legal_col(*it))
				return false;
			out->destination += (*it - '1');
			parse_state = DONE;
		} break;
		}
	}
	return parse_state == DONE;
}

bool check_move_legality_and_get_flags(ChessGame* game, Move* out_move) {
	bool is_legal = false;
	if (game->current_turn != game->board[out_move->source].team())
		return false;
	foreach_piece_legal_move(game, out_move->source, [&is_legal, out_move](Move move)
	{
		if (move.destination == out_move->destination) {
			is_legal = true;
			out_move->flags = move.flags;
			return IterationStatus::BREAK;
		}
		return IterationStatus::CONTINUE;
	});

	return is_legal;
}


enum class GameStatus
{
	WIN,
	DRAW,
	CONTINUE,
};

GameStatus get_game_status(ChessGame* game) {
	bool has_moves = false;
	foreach_team_legal_move(game, game->current_turn,
		[&has_moves](Move)
	{
		has_moves = true;
		return IterationStatus::BREAK;
	}, true);
	if (has_moves)
		return GameStatus::CONTINUE;

	bool is_check = false;
	Team other_team = game->current_turn == Team::WHITE ? Team::BLACK : Team::WHITE;
	foreach_team_legal_move(game, other_team,
		[&is_check](Move move)
	{
		if (move.flags & MoveFlags::ATTACK
			&& move.get_attacked_piece_type() == PieceType::KING) {
			is_check = true;
			return IterationStatus::BREAK;
		}
		return IterationStatus::CONTINUE;
	}, false);
	if (is_check)
		return GameStatus::WIN;
	else
		return GameStatus::DRAW;
}

int evaluate_board(ChessGame* game) {
	++boards_evaluated;

	int result = 0;

	bool is_early_stage = pieces_on_board_count(game) > 24;

	for (uint8_t i = 0; i < 8 * 8; ++i) {
		Piece piece = game->board[i];
		if (piece.type() == PieceType::NONE)
			continue;

		int16_t piece_value = 0;

		switch (piece.type()) {
		case PieceType::PAWN: {
			piece_value = 1;
		} break;
		case PieceType::BISHOP: {
			piece_value = 3;
		} break;
		case PieceType::KNIGHT: {
			piece_value = 3;
		} break;
		case PieceType::ROOK: {
			piece_value = 5;
		} break;
		case PieceType::QUEEN: {
			piece_value = 9;
		} break;
		case PieceType::KING: {
			piece_value = 100;

			// In early stage we consider the king being in the corners as a good thing
			// and the opposite in late stage.
			uint8_t col = i % 8;
			uint8_t row = i / 8;
			if (is_early_stage) {
				piece_value += (absolute_value(col - 3) + absolute_value(row - 3)) / 2;
			} else {
				piece_value -= (absolute_value(col - 3) + absolute_value(row - 3)) / 2;
			}
		} break;
		}

		if (piece.team() == Team::BLACK)
			piece_value = -piece_value;

		result += piece_value;
	}

	return result;
}

inline int min(int a, int b) {
	return a < b ? a : b;
}

inline int max(int a, int b) {
	return a > b ? a : b;
}

int minimax(ChessGame* game, Move move, bool is_max_player, int8_t depth, int alpha, int beta) {
	int result;
	performe_move(game, move);
	if (depth <= 0)
		result = evaluate_board(game);
	else {
		if (!move.flags & MoveFlags::ATTACK) {
			depth -= 1;
		}

		if (is_max_player) {
			result = std::numeric_limits<int>::min();
			foreach_team_legal_move(game, game->current_turn,
				[&alpha, &beta, &result, game, depth](Move move)
			{
				result = max(result, minimax(game, move, false, depth - 1, alpha, beta));
				alpha = max(result, alpha);
				if (alpha >= beta)
					return IterationStatus::BREAK;
				return IterationStatus::CONTINUE;
			}, true);
		} else {
			result = std::numeric_limits<int>::max();
			foreach_team_legal_move(game, game->current_turn,
				[&alpha, &beta, &result, game, depth](Move move)
			{
				result = min(result, minimax(game, move, true, depth - 1, alpha, beta));
				beta = min(result, beta);
				if (alpha >= beta)
					return IterationStatus::BREAK;
				return IterationStatus::CONTINUE;
			}, true);
		}
	}
	undo_last_move(game);

	return result;
}

inline bool greater_than(int a, int b) {
	return a > b;
}

inline bool less_than(int a, int b) {
	return a < b;
}

Move get_best_next_move(ChessGame* game, int depth) {
	Move best_move;
	int best_move_score;
	bool(*is_better_predicate)(int, int) = nullptr;
	using limits = std::numeric_limits<int>;

	if (game->current_turn == Team::WHITE) {
		is_better_predicate = greater_than;
		best_move_score = limits::min();
	} else {
		is_better_predicate = less_than;
		best_move_score = limits::max();
	}

	boards_evaluated = 0;

	// HACK!!! :( 
	// Because sometimes (observed in end of games, so far)
	// minimax always return limits::min (or max, in case
	// of black), we have to make sure it chooses a move,
	// so we choose randomly in case of two moves with equal score.
	srand(time(nullptr));
	int equal_moves_considerd = 0;
	foreach_team_legal_move(game, game->current_turn,
		[is_better_predicate, &equal_moves_considerd, &best_move, &best_move_score, game, depth](Move move)
	{
		int move_score = minimax(game, move, game->current_turn != Team::WHITE, depth, limits::min(), limits::max());
		if (is_better_predicate(move_score, best_move_score)) {
			best_move_score = move_score;
			best_move = move;
			equal_moves_considerd = 0;
		} else if (move_score == best_move_score && rand() % ++equal_moves_considerd == 0) {
			best_move_score = move_score;
			best_move = move;
		}
		return IterationStatus::CONTINUE;
	}, true);
	printf("Evaluated boards: %llu\n", boards_evaluated);
	printf("Best move: ");
	print_move(best_move);
	printf("With score: %d\n", best_move_score);

	return best_move;
}

bool full_test(ChessGame* game, Move move, int depth) {
	Piece board_copy[8 * 8];
	GameFlags flags_copy = game->flags;
	memcpy(board_copy, game->board, sizeof(Piece) * 8 * 8);

	performe_move(game, move);

	if (depth != 0) {
		bool has_yield = foreach_team_legal_move(game, game->current_turn,
			[game, depth](Move move)
		{
			if (full_test(game, move, depth - 1))
				return IterationStatus::CONTINUE;
			return IterationStatus::BREAK;
		}, true);
		if (has_yield)
			return false;
	}

	undo_last_move(game);

	bool is_equal = memcmp(board_copy, game->board, sizeof(Piece) * 8 * 8) == 0;
	if (!is_equal) {
		printf("-------------------\n");
		printf("Failure to undo had result in the following board:\n");
		print_board(game);
		memcpy(game->board, board_copy, sizeof(Piece) * 8 * 8);
		printf("With the following move:\n");
		print_move(move);
		printf("In depth %d.\n", depth);
	}
	if (flags_copy != game->flags) {
		is_equal = false;
		printf("-------------------\n");
		printf("Flags inequality!\n");
		printf("Excpected: %d, Got: %d.\n", flags_copy, game->flags);
	}
	return is_equal;
}

void print_game_flags(GameFlags flags) {
	if (flags & CAN_WHITE_CASTLE_RIGHT)
		printf("CAN_WHITE_CASTLE_RIGHT\n");
	if (flags & CAN_WHITE_CASTLE_LEFT)
		printf("CAN_WHITE_CASTLE_LEFT\n");
	if (flags & CAN_BLACK_CASTLE_RIGHT)
		printf("CAN_BLACK_CASTLE_RIGHT\n");
	if (flags & CAN_BLACK_CASTLE_LEFT)
		printf("CAN_BLACK_CASTLE_LEFT\n");
}

void print_history(MoveHistory* history) {
	for (int i = 0; i < history->cursor; ++i)
		print_move(history->moves[i]);
}

bool maybe_parse_and_exceute_command(ChessGame* game, const char* input) {
	if (strcmp(input, "undo") == 0) {
		if (game->history.cursor <= 1)
			return false;
		undo_last_move(game);
		if (game->is_against_ai)
			undo_last_move(game);
		return true;
	} else if (strcmp(input, "eval") == 0) {
		printf("Board score: %i.\n", evaluate_board(game));
		return true;
	} else if (strcmp(input, "list") == 0) {
		char input_piece[2] = { 0 };
		while (true) {
			printf("Enter piece location: ");
			scanf_s("%s", input_piece, 2);
			if (input_piece[0] < 'a' || input_piece[0] > 'h'
				|| input_piece[1] < '1' || input_piece[1] > '8') {
				printf("Invalid input!\n");
				continue;
			}
			uint8_t index = (input_piece[0] - 'a') * 8 + (input_piece[1] - '1');
			Piece piece = game->board[index];
			if (piece.type() == PieceType::NONE) {
				printf("ERROR: No piece at the location asked.\n");
				continue;
			}
			foreach_piece_legal_move(game, index,
				[](Move move)
			{
				print_move(move);
				return IterationStatus::CONTINUE;
			}, true);
			break;
		}
		return true;
	} else if (strcmp(input, "test") == 0) {
		foreach_team_legal_move(game, game->current_turn,
			[game](Move move)
		{
			performe_move(game, move);
			undo_last_move(game);
			return IterationStatus::CONTINUE;
		}, true);
		return true;
	} else if (strcmp(input, "full") == 0) {
		int levels = 4;
		printf("Performing full test in %d levels...\n", levels);
		bool has_passed = true;
		foreach_team_legal_move(game, game->current_turn,
			[&has_passed, game, levels](Move move)
		{
			if (full_test(game, move, levels)) {
				has_passed = false;
				return IterationStatus::CONTINUE;
			}
			return IterationStatus::BREAK;
		}, true);
		if (has_passed)
			printf("Test passed successfully!\n");
		return true;
	} else if (strcmp(input, "flag") == 0) {
		print_game_flags(game->flags);
		return true;
	} else if (strcmp(input, "hist") == 0) {
		print_history(&game->history);
		return true;
	} else if (strcmp(input, "exit") == 0) {
		exit(0);
	}
	return false;
}

void game_loop(ChessGame* game) {
	GameStatus status = GameStatus::CONTINUE;
	char input[5] = { 0 };
	while (status == GameStatus::CONTINUE) {
		Move move;
		while (true) {
			print_board(game);

			if (game->is_against_ai && game->current_turn == Team::BLACK) {
				printf("Calculating next move...\n");
				move = get_best_next_move(game, SEARCH_DEPTH);
				break;
			} else {
				printf("Enter move instruction (like 'b2d2'):\n");
				scanf_s("%s", &input, 5);

				if (maybe_parse_and_exceute_command(game, input))
					continue;
				if (parse_move(input, &move) && check_move_legality_and_get_flags(game, &move))
					break;
				else {
					printf("\u001b[31;1m"); // Red foreground.
					printf("--- Ilegal instruction! ---\n");
					printf("\033[0m"); // Color reset.
				}
			}
		}

		performe_move(game, move);
		status = get_game_status(game);
	}

	print_board(game);

	if (status == GameStatus::WIN) {
		printf("Checkmate!\n");
		printf("Enter 'hist' to print the game's history, anything else to exit: ");
		scanf_s("%s", input, 5);
		if (input == "hist")
			print_history(&game->history);
	} else {
		printf("Draw!\n");
	}
}

int main() {
	ChessGame cg;
	init_game(&cg);
	game_loop(&cg);
	return 0;
}
