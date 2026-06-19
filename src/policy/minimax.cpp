#include <algorithm>
#include <utility>
#include <vector>
#include "state.hpp"
#include "minimax.hpp"


/*============================================================
 * MiniMax — eval_ctx
 *
 * Negamax without pruning. Caller manages memory.
 *============================================================*/
int MiniMax::eval_ctx(
    State *state,
    int depth,
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

    /* === Lazy move generation (sets game_state) === */
    if(state->legal_actions.empty() && state->game_state == UNKNOWN){
        state->get_legal_actions();
    }

    /* === Terminal / leaf checks === */

    // [ Hackathon TODO 3-1 ]
    // return the score for a winning terminal state
    // Hint: prefer faster wins by using ply.
    if(state->game_state == WIN){
        return P_MAX - ply;
    }

    if(state->game_state == DRAW){
        return 0;
    }

    /* === Repetition check (game-specific) === */
    int rep_score;
    if(state->check_repetition(history, rep_score)){
        return rep_score;
    }
    history.push(state->hash());

    if(depth <= 0){
        int score = state->evaluate(
            p.use_kp_eval, p.use_eval_mobility, &history
        ); 
        history.pop(state->hash());
        return score;
    }

    /* === Negamax loop === */
    int best_score = M_MAX;

    for(auto& action : state->legal_actions){
        // [ Hackathon TODO 3-2 ]
        // create the child state after applying action
        State* next = state->next_state(action);

        bool same = next->same_player_as_parent();

        // [Hackathon TODO 3-3]
        // search the child one level deeper
        int raw = eval_ctx(next, depth - 1, history, ply + 1, ctx, p);

        // [Hackathon TODO 3-4]
        // convert raw to the current player's perspective.
        int score = same ? raw : -raw;

        delete next;

        // [ Hackathon TODO 3-5 ]
        // update best_score if this child is better.
        // 分數已經轉回目前玩家視角，所以直接取最大值。
        if(score > best_score){
            best_score = score;
        }

    }

    history.pop(state->hash());
    return best_score;
}

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
        // 吃王永遠排最前面，這種局面不用讓普通吃子或中心分干擾。
        score += 10000000;
    }else if(captured){
        // MVV-LVA：先吃價值高的棋，若目標相同，低價攻擊子優先。
        score += 1000000 + 100 * piece_value(captured) - piece_value(attacker);
    }

    if(attacker == 1){
        // 兵越靠近升變列越值得先看，但這只是小加分，不能蓋過吃子。
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
        // 這個檢查只放在 root，先把會立刻送王的棋往後排。
        losing = side_to_move_can_capture_king(next);
    }

    delete next;
    return losing;
}

static std::vector<Move> ordered_root_actions(State *state){
    std::vector<Move> actions = ordered_actions(state);

    std::stable_partition(
        actions.begin(),
        actions.end(),
        [state](const Move& action){
            return !gives_opponent_immediate_king_capture(state, action);
        }
    );

    return actions;
}

static constexpr int QSEARCH_DEPTH = 2; // 2就好

static bool is_tactical_move(State *state, const Move& action){
    Point to = action.second;
    int captured = state->board.board[1 - state->player][to.first][to.second];

    if(captured == 6){
        return true;
    }
    if(captured != 0){
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

    // 先用目前局面當 stand-pat，只有吃子或吃王這種戰術步才繼續延伸。
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

        // 同一方續走就維持 window；換手才把 alpha/beta 反向給 child。
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

static int alphabeta_ctx(
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

    // 需要用到局面狀態時才生合法步，避免還沒展開的節點多做事。
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
        // alphabeta 已經 push 過這個局面；先 pop 再進 qsearch，避免 repetition history 重複記同一手。
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

    std::vector<Move> actions = ordered_actions(state);
    for(auto& action : actions){
        if(ctx.stop){
            break;
        }

        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw;

        // 同一位玩家續走時視角不變；換手時才使用 negamax 的反向 window。
        if(same){
            raw = alphabeta_ctx(next, depth - 1, alpha, beta, history, ply + 1, ctx, p);
        }else{
            raw = alphabeta_ctx(next, depth - 1, -beta, -alpha, history, ply + 1, ctx, p);
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


/*============================================================
 * MiniMax — search
 *
 * Iterate legal moves, call alphabeta_ctx, return SearchResult.
 *============================================================*/
SearchResult MiniMax::search(
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
    std::vector<Move> root_actions = ordered_root_actions(state);

    // 先回報一手保底合法棋，避免搜尋還沒結束就被外部 runner 中斷。
    result.best_move = root_actions.front();
    result.score = 0;
    result.pv = {result.best_move};
    if(p.report_partial && ctx.on_root_update){
        ctx.on_root_update({result.best_move, result.score, depth, 0, total_moves});
    }

    if(state->game_state == WIN){
        // WIN 代表目前有棋可以吃王，但不保證吃王棋在第一個，所以這裡要明確找出來。
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

        /* [ Hackathon TODO 4-1 ]
         * search this move like TODO 3, but starting from the root */
        State* next = state->next_state(action);
        bool same = next->same_player_as_parent();
        int raw;
        if(same){
            raw = alphabeta_ctx(next, depth - 1, alpha, beta, history, 1, ctx, p);
        }else{
            raw = alphabeta_ctx(next, depth - 1, -beta, -alpha, history, 1, ctx, p);
        }
        delete next;

        if(ctx.stop){
            break;
        }

        int score = same ? raw : -raw;

        if(!searched_any || score > best_score){
            // [ Hackathon TODO 4-2 ]
            // keep this move if it is the best so far
            searched_any = true;
            best_score = score;
            result.best_move = action;
            result.score = best_score;
            result.pv = {result.best_move};

            // 找到更好的 root move 就立刻回報，讓 2 秒制下也能拿到目前最佳解。
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

    // [ Hackathon TODO 4-3 ]
    // update result and return
    if(searched_any){
        result.score = best_score;
    }
    result.nodes = ctx.nodes;
    result.seldepth = ctx.seldepth;
    result.pv = {result.best_move};

    return result;
} 


/*============================================================
 * MiniMax — default_params / param_defs
 *============================================================*/
ParamMap MiniMax::default_params(){
    return {
        {"UseKPEval", "true"},
        {"UseEvalMobility", "true"},
        {"ReportPartial", "true"},
    };
}

std::vector<ParamDef> MiniMax::param_defs(){
    return {
        {"UseKPEval", ParamDef::CHECK, "true"},
        {"UseEvalMobility", ParamDef::CHECK, "true"},
        {"ReportPartial", ParamDef::CHECK, "true"},
    };
}
