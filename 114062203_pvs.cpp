#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "state.hpp"
#include "114062203_pvs.hpp"

static int piece_value(int piece){
    switch(piece){
        case 1: return 100;     // pawn
        case 2: return 500;     // rook
        case 3: return 320;     // knight
        case 4: return 330;     // bishop
        case 5: return 900;     // queen
        case 6: return 100000;  // king
        default: return 0;
    }
}

static int move_order_score(State *state, const Move& action){
    Point from = action.first;
    Point to = action.second;
    int attacker = state->board.board[state->player][from.first][from.second];
    int captured = state->board.board[1 - state->player][to.first][to.second];
    int score = 0;

    if(captured == 6){
        score += 10000000;
    }else if(captured){
        score += 1000000 + 100 * piece_value(captured) - piece_value(attacker);
    }

    if(attacker == 1){
        int promotion_row = (state->player == 0) ? 0 : BOARD_H - 1;
        int dist = std::abs((int)to.first - promotion_row);
        score += 200 - 20 * dist;
        if(dist == 0){
            score += 3000;
        }
    }

    int row = (int)to.first;
    int col = (int)to.second;
    int row_dist = std::min(std::abs(row - 2), std::abs(row - 3));
    int col_dist = std::abs(col - 2);
    score += 30 - 5 * (row_dist + col_dist);

    return score;
}

static std::vector<Move> ordered_actions(State *state){
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(state->legal_actions.size());

    for(auto& action : state->legal_actions){
        scored.push_back({move_order_score(state, action), action});
    }

    std::stable_sort(
        scored.begin(),
        scored.end(),
        [](const auto& a, const auto& b){
            return a.first > b.first;
        }
    );

    std::vector<Move> actions;
    actions.reserve(scored.size());
    for(auto& item : scored){
        actions.push_back(item.second);
    }
    return actions;
}

static constexpr int MAX_KILLER_PLY = 128;
static constexpr int HISTORY_BONUS_LIMIT = 800000;
static Move killer_moves[MAX_KILLER_PLY][2];
static int history_score[2][BOARD_H][BOARD_W][BOARD_H][BOARD_W];

struct HashMoveEntry {
    int depth;
    Move best_move;
};

static std::unordered_map<unsigned long long, HashMoveEntry> hash_best_moves;
static constexpr size_t HASH_MOVE_MAX_SIZE = 1000000;

static bool same_move(const Move& a, const Move& b){
    return a.first.first == b.first.first
        && a.first.second == b.first.second
        && a.second.first == b.second.first
        && a.second.second == b.second.second;
}

static bool is_capture_move(State *state, const Move& action){
    Point to = action.second;
    int captured = state->board.board[1 - state->player][to.first][to.second];
    return captured != 0;
}

static bool is_killer_move(const Move& action, int ply){
    if(ply < 0 || ply >= MAX_KILLER_PLY){
        return false;
    }
    return same_move(action, killer_moves[ply][0])
        || same_move(action, killer_moves[ply][1]);
}

static void record_killer_move(const Move& action, int ply){
    if(ply < 0 || ply >= MAX_KILLER_PLY){
        return;
    }
    if(same_move(action, killer_moves[ply][0])){
        return;
    }

    killer_moves[ply][1] = killer_moves[ply][0];
    killer_moves[ply][0] = action;
}

static void record_history_move(State *state, const Move& action, int depth){
    Point from = action.first;
    Point to = action.second;
    int& score = history_score[state->player][from.first][from.second][to.first][to.second];
    score += depth * depth;
    if(score > HISTORY_BONUS_LIMIT){
        score = HISTORY_BONUS_LIMIT;
    }
}

static int move_order_score_with_history(State *state, const Move& action, int ply){
    int score = move_order_score(state, action);

    if(!is_capture_move(state, action) && is_killer_move(action, ply)){
        score += 900000;
    }

    Point from = action.first;
    Point to = action.second;
    int hist = history_score[state->player][from.first][from.second][to.first][to.second];
    if(hist > HISTORY_BONUS_LIMIT){
        hist = HISTORY_BONUS_LIMIT;
    }
    score += hist;

    return score;
}

static bool get_hash_best_move(State *state, Move& best_move, int depth){
    unsigned long long key = (unsigned long long)state->hash();
    auto it = hash_best_moves.find(key);
    if(it == hash_best_moves.end()){
        return false;
    }

    if(it->second.depth >= depth - 1 || it->second.depth >= 0){
        best_move = it->second.best_move;
        return true;
    }
    return false;
}

static void record_hash_best_move(State *state, const Move& best_move, int depth){
    if(hash_best_moves.size() > HASH_MOVE_MAX_SIZE){
        hash_best_moves.clear();
    }

    unsigned long long key = (unsigned long long)state->hash();
    auto it = hash_best_moves.find(key);
    if(it != hash_best_moves.end() && it->second.depth >= depth){
        return;
    }

    hash_best_moves[key] = {depth, best_move};
}

static std::vector<Move> ordered_actions_with_hash_history(State *state, int ply, int depth){
    Move hash_move;
    bool has_hash_move = get_hash_best_move(state, hash_move, depth);
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(state->legal_actions.size());

    for(auto& action : state->legal_actions){
        int score = move_order_score_with_history(state, action, ply);
        if(has_hash_move && same_move(action, hash_move)){
            score += 2000000;
        }
        scored.push_back({score, action});
    }

    std::stable_sort(
        scored.begin(),
        scored.end(),
        [](const auto& a, const auto& b){
            return a.first > b.first;
        }
    );

    std::vector<Move> actions;
    actions.reserve(scored.size());
    for(auto& item : scored){
        actions.push_back(item.second);
    }
    return actions;
}

static bool side_to_move_can_capture_king(State *state){
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    for(auto& action : state->legal_actions){
        Point to = action.second;
        if(state->board.board[1 - state->player][to.first][to.second] == 6){
            return true;
        }
    }
    return false;
}

static bool gives_opponent_immediate_king_capture(State *state, const Move& action){
    State* next = state->next_state(action);
    bool opponent_turn = !next->same_player_as_parent();
    bool losing = false;

    if(opponent_turn){
        losing = side_to_move_can_capture_king(next);
    }

    delete next;
    return losing;
}

static std::vector<Move> ordered_root_actions(State *state, int depth){
    Move hash_move;
    bool has_hash_move = get_hash_best_move(state, hash_move, depth);
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(state->legal_actions.size());

    for(auto& action : state->legal_actions){
        int score = move_order_score(state, action);
        if(has_hash_move && same_move(action, hash_move)){
            score += 2000000;
        }
        if(gives_opponent_immediate_king_capture(state, action)){
            score -= 50000000;
        }
        scored.push_back({score, action});
    }

    std::stable_sort(
        scored.begin(),
        scored.end(),
        [](const auto& a, const auto& b){
            return a.first > b.first;
        }
    );

    std::vector<Move> actions;
    actions.reserve(scored.size());
    for(auto& item : scored){
        actions.push_back(item.second);
    }
    return actions;
}

static constexpr int QSEARCH_DEPTH = 3;

static bool is_promotion_move(State *state, const Move& action){
    Point from = action.first;
    Point to = action.second;
    int piece = state->board.board[state->player][from.first][from.second];
    return piece == 1 && (to.first == 0 || to.first == BOARD_H - 1);
}

static bool is_tactical_move(State *state, const Move& action){
    Point to = action.second;
    int captured = state->board.board[1 - state->player][to.first][to.second];

    if(captured == 6){
        return true;
    }
    if(captured != 0){
        return true;
    }
    if(is_promotion_move(state, action)){
        return true;
    }
    return false;
}

static std::vector<Move> ordered_tactical_actions(State *state){
    std::vector<Move> ordered = ordered_actions(state);
    std::vector<Move> tactical;
    tactical.reserve(ordered.size());

    for(auto& action : ordered){
        if(is_tactical_move(state, action)){
            tactical.push_back(action);
        }
    }

    return tactical;
}

static int quiescence_ctx(
    State *state,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    int qdepth,
    SearchContext& ctx,
    const MMParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }

    if(state->game_state == DRAW){
        return 0;
    }

    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    int stand_pat = state->evaluate(
        p.use_kp_eval, p.use_eval_mobility, &history
    );

    if(stand_pat >= beta){
        history.pop(state->hash());
        return stand_pat;
    }

    if(stand_pat > alpha){
        alpha = stand_pat;
    }

    int best_score = stand_pat;

    if(qdepth <= 0){
        history.pop(state->hash());
        return best_score;
    }

    std::vector<Move> actions = ordered_tactical_actions(state);
    for(auto& action : actions){
        if(ctx.stop){
            break;
        }

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw;

        if(same){
            raw = quiescence_ctx(next, alpha, beta, history, ply + 1, qdepth - 1, ctx, p);
        }else{
            raw = quiescence_ctx(next, -beta, -alpha, history, ply + 1, qdepth - 1, ctx, p);
        }

        delete next;

        if(ctx.stop){
            break;
        }

        int score = same ? raw : -raw;

        if(score > best_score){
            best_score = score;
        }
        if(score > alpha){
            alpha = score;
        }
        if(alpha >= beta){
            break;
        }
    }

    history.pop(state->hash());
    return best_score;
}

static int pvs_ctx(
    State *state,
    int depth,
    int alpha,
    int beta,
    GameHistory& history,
    int ply,
    SearchContext& ctx,
    const MMParams& p
){
    ctx.nodes++;
    if(ply > ctx.seldepth){
        ctx.seldepth = ply;
    }
    if(ctx.stop){
        return 0;
    }

    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    if(state->game_state == WIN){
        return P_MAX - ply;
    }

    if(state->game_state == DRAW){
        return 0;
    }

    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    if(depth <= 0){
        history.pop(state->hash());
        return quiescence_ctx(
            state,
            alpha,
            beta,
            history,
            ply,
            QSEARCH_DEPTH,
            ctx,
            p
        );
    }

    int best_score = M_MAX;
    Move best_move;
    bool has_best_move = false;
    bool first_child = true;

    std::vector<Move> actions = ordered_actions_with_hash_history(state, ply, depth);
    for(auto& action : actions){
        if(ctx.stop){
            break;
        }

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw;
        int score;

        if(first_child){
            if(same){
                raw = pvs_ctx(next, depth - 1, alpha, beta, history, ply + 1, ctx, p);
                score = raw;
            }else{
                raw = pvs_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
                score = -raw;
            }
        }else if(same){
            raw = pvs_ctx(next, depth - 1, alpha, alpha + 1, history, ply + 1, ctx, p);
            score = raw;

            if(score > alpha && score < beta){
                raw = pvs_ctx(next, depth - 1, alpha, beta, history, ply + 1, ctx, p);
                score = raw;
            }
        }else{
            raw = pvs_ctx(next, depth - 1, -alpha - 1, -alpha, history, ply + 1, ctx, p);
            score = -raw;

            if(score > alpha && score < beta){
                raw = pvs_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
                score = -raw;
            }
        }

        delete next;

        if(ctx.stop){
            break;
        }

        if(score > best_score){
            best_score = score;
            best_move = action;
            has_best_move = true;
        }

        if(score > alpha){
            alpha = score;
        }

        if(alpha >= beta){
            if(!is_capture_move(state, action)){
                record_killer_move(action, ply);
                record_history_move(state, action, depth);
            }
            break;
        }

        first_child = false;
    }

    if(!ctx.stop && has_best_move){
        record_hash_best_move(state, best_move, depth);
    }

    history.pop(state->hash());
    return best_score;
}

SearchResult PVS::search(
    State *state,
    int depth,
    GameHistory& history,
    SearchContext& ctx
){
    ctx.reset();
    MMParams p = MMParams::from_map(ctx.params);
    SearchResult result;
    result.depth = depth;

    if(!state->legal_actions.size()){
        state->get_legal_actions();
    }

    if(state->legal_actions.empty()){
        result.best_move = Move();
        result.score = 0;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        return result;
    }

    int total_moves = (int)state->legal_actions.size();
    std::vector<Move> root_actions = ordered_root_actions(state, depth);

    result.best_move = root_actions.front();
    result.score = 0;
    result.pv = {result.best_move};
    if(p.report_partial && ctx.on_root_update){
        ctx.on_root_update({result.best_move, result.score, depth, 0, total_moves});
    }

    if(state->game_state == WIN){
        for(auto& action : state->legal_actions){
            Point to = action.second;
            if(state->board.board[1 - state->player][to.first][to.second] == 6){
                result.best_move = action;
                break;
            }
        }
        result.score = P_MAX;
        result.nodes = ctx.nodes;
        result.seldepth = ctx.seldepth;
        result.pv = {result.best_move};
        if(p.report_partial && ctx.on_root_update){
            ctx.on_root_update({result.best_move, result.score, depth, 1, total_moves});
        }
        return result;
    }

    int alpha = M_MAX;
    int beta = P_MAX;
    int best_score = M_MAX;
    bool searched_any = false;
    int move_index = 0;

    for(auto& action : root_actions){
        if(ctx.stop){
            break;
        }

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw;
        if(same){
            raw = pvs_ctx(next, depth - 1, alpha, beta, history, 1, ctx, p);
        }else{
            raw = pvs_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
        }
        delete next;

        if(ctx.stop){
            break;
        }

        int score = same ? raw : -raw;

        if(!searched_any || score > best_score){
            searched_any = true;
            best_score = score;
            result.best_move = action;
            result.score = best_score;
            result.pv = {result.best_move};

            if(p.report_partial && ctx.on_root_update){
                ctx.on_root_update({result.best_move, best_score, depth, move_index + 1, total_moves});
            }
        }else{
            searched_any = true;
        }

        if(score > alpha){
            alpha = score;
        }
        if(alpha >= beta){
            break;
        }
        move_index++;
    }

    if(searched_any){
        result.score = best_score;
    }
    if(searched_any && !ctx.stop){
        record_hash_best_move(state, result.best_move, depth);
    }
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    result.pv = {result.best_move};

    return result;
}

ParamMap PVS::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> PVS::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
