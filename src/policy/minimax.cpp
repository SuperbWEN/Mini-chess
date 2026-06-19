#include <utility>
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
        int score = state->evaluate(
            p.use_kp_eval, p.use_eval_mobility, &history
        );
        history.pop(state->hash());
        return score;
    }

    int best_score = M_MAX;

    for(auto& action : state->legal_actions){
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

    // 先回報一手保底合法棋，避免搜尋還沒結束就被外部 runner 中斷。
    result.best_move = state->legal_actions.front();
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

    for(auto& action : state->legal_actions){
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
