// roguelike.cpp
// Single-file tiny roguelike with difficulty, potions, high score, and basic enemy pathing.
// Compile: g++ -std=c++17 -O2 -o roguelike roguelike.cpp
// Run: ./roguelike
//
// Controls: w=up a=left s=down d=right (press key + Enter). q to quit.
// Pick difficulty at start. Potions '!' heal 6-10 HP (capped). Enemies 'E' pathfind one tile toward player.
// Score +10 per kill. High score saved to highscore.txt.

#include <bits/stdc++.h>
using namespace std;

const int MAP_W = 20;
const int MAP_H = 10;

struct Rect { int x,y,w,h; int centerX()const{return x+w/2;} int centerY()const{return y+h/2;} 
    bool intersects(const Rect& r) const {
        return !(x + w <= r.x || r.x + r.w <= x || y + h <= r.y || r.y + r.h <= y);
    }
};

struct Enemy { int x,y; int hp; bool alive; };

struct Item { int x,y; }; // only health potions

using Grid = array<array<char, MAP_W>, MAP_H>;

// RNG
static std::mt19937 rng((unsigned)chrono::high_resolution_clock::now().time_since_epoch().count());
int rnd(int a,int b){ std::uniform_int_distribution<int> d(a,b); return d(rng); }

// Difficulty config
enum Difficulty { EASY=0, NORMAL=1, HARD=2 };
struct DiffConfig {
    int enemyMin, enemyMax;
    int enemyHpMin, enemyHpMax;
    int enemyAtkMin, enemyAtkMax;
    int potionMin, potionMax;
};
DiffConfig diffConfigs[3];

// Map helpers
void create_empty_map(Grid &map){
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) map[y][x] = '#';
}
void carve_room(Grid &map, const Rect &r){
    for(int yy=r.y; yy<r.y+r.h; ++yy) for(int xx=r.x; xx<r.x+r.w; ++xx)
        if (xx>=0 && xx<MAP_W && yy>=0 && yy<MAP_H) map[yy][xx]='.';
}
void carve_h(Grid &map,int x1,int x2,int y){
    if(x2<x1) swap(x1,x2);
    for(int x=x1;x<=x2;++x) if(x>=0 && x<MAP_W && y>=0 && y<MAP_H) map[y][x]='.';
}
void carve_v(Grid &map,int y1,int y2,int x){
    if(y2<y1) swap(y1,y2);
    for(int y=y1;y<=y2;++y) if(x>=0 && x<MAP_W && y>=0 && y<MAP_H) map[y][x]='.';
}

pair<int,int> random_floor_tile(const Grid &map){
    vector<pair<int,int>> floors;
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) if (map[y][x]=='.') floors.emplace_back(x,y);
    if(floors.empty()) return {1,1};
    return floors[rnd(0,(int)floors.size()-1)];
}

// BFS pathfinding for single-step: returns next (nx,ny) from (sx,sy) to move one tile toward (tx,ty).
// Avoid tiles not floor, and avoid cells occupied by other enemies (occupied vector).
// If no path found, returns sx,sy (stay). If next tile is target (tx,ty), returns target.
pair<int,int> bfs_next_step(const Grid &map, int sx, int sy, int tx, int ty, const vector<pair<int,int>>& occupied){
    if (sx==tx && sy==ty) return {sx,sy};
    // grid of visited
    array<array<bool, MAP_W>, MAP_H> vis{};
    array<array<pair<int,int>, MAP_W>, MAP_H> parent;
    queue<pair<int,int>> q;
    q.push({sx,sy}); vis[sy][sx] = true;
    // mark occupied as blocked except the final target (player) — enemies can step onto player
    auto blocked = [&](int x,int y)->bool{
        if (x<0||x>=MAP_W||y<0||y>=MAP_H) return true;
        if (map[y][x]!='.') {
            // allow stepping into player's tile which may be on floor; if player is on floor it's fine, but we only allow '.' or player tile
            // but map contains only '#' or '.' so if not '.' it's '#', blocked.
            return true;
        }
        for(auto &p: occupied) if (p.first==x && p.second==y && !(x==tx && y==ty)) return true;
        return false;
    };
    vector<pair<int,int>> dirs = {{1,0},{-1,0},{0,1},{0,-1}};
    bool found=false;
    while(!q.empty()){
        auto cur = q.front(); q.pop();
        if (cur.first==tx && cur.second==ty){ found = true; break; }
        for(auto &d:dirs){
            int nx = cur.first + d.first, ny = cur.second + d.second;
            if(nx<0||nx>=MAP_W||ny<0||ny>=MAP_H) continue;
            if(vis[ny][nx]) continue;
            if(blocked(nx,ny)) continue;
            vis[ny][nx]=true;
            parent[ny][nx] = cur;
            q.push({nx,ny});
        }
    }
    if(!found) {
        // try greedy fallback: step toward player in x or y if available (no BFS path)
        int dx = (tx>sx)?1:((tx<sx)?-1:0);
        int dy = (ty>sy)?1:((ty<sy)?-1:0);
        // prefer larger delta
        if (abs(tx-sx) >= abs(ty-sy)) {
            if (!blocked(sx+dx, sy)) return {sx+dx, sy};
            if (!blocked(sx, sy+dy)) return {sx, sy+dy};
        } else {
            if (!blocked(sx, sy+dy)) return {sx, sy+dy};
            if (!blocked(sx+dx, sy)) return {sx+dx, sy};
        }
        return {sx,sy};
    }
    // backtrack from target to start to find first step
    pair<int,int> cur = {tx,ty};
    pair<int,int> prev = parent[cur.second][cur.first];
    while(!(prev.first==sx && prev.second==sy)){
        cur = prev;
        prev = parent[cur.second][cur.first];
    }
    return cur; // this is the first step from start
}

// Render & UI
void print_header(){
    cout << "=== Tiny Roguelike ===\n";
    cout << "Controls: w=up a=left s=down d=right    q=quit\n";
    cout << "Objective: survive, kill enemies (score +10 per kill), pick potions '!' to heal.\n";
    cout << "High score saved in highscore.txt\n\n";
}

void render(const Grid &map, const vector<Enemy>& enemies, const vector<Item>& items,
            int px,int py,int playerHP,int playerMaxHP,int score,int turns,int highScore, Difficulty diff) {
    print_header();
    string diffName = (diff==EASY?"Easy":(diff==NORMAL?"Normal":"Hard"));
    cout << "Diff: " << diffName << "    HP: " << playerHP << "/" << playerMaxHP
         << "    Score: " << score << "    Turns: " << turns << "    High: " << highScore << "\n\n";
    Grid draw = map;
    for(auto &it: items) if (it.x>=0 && it.y>=0) draw[it.y][it.x] = '!';
    for(auto &e: enemies) if (e.alive) draw[e.y][e.x] = 'E';
    if (px>=0 && py>=0) draw[py][px] = '@';
    for(int y=0;y<MAP_H;y++){
        for(int x=0;x<MAP_W;x++) cout << draw[y][x];
        cout << '\n';
    }
    cout << '\n';
}

// high score IO
int load_high_score(const string &fname){
    ifstream ifs(fname);
    if(!ifs) return 0;
    int h=0; ifs>>h; return h;
}
void save_high_score(const string &fname, int h){
    ofstream ofs(fname);
    if(!ofs){ cerr << "Warning: could not write high score\n"; return; }
    ofs<<h<<"\n";
}

// Global-ish: we will group generation code into regenerate_map
void generate_map_basic(Grid &map, vector<Rect> &rooms) {
    create_empty_map(map);
    rooms.clear();
    int maxRooms = 6;
    int roomCount = rnd(3, maxRooms);
    for(int i=0;i<roomCount;i++){
        Rect r;
        r.w = rnd(3,8);
        r.h = rnd(3,5);
        r.x = rnd(1, MAP_W - r.w - 1);
        r.y = rnd(1, MAP_H - r.h - 1);
        bool ok=true;
        for(auto &o: rooms) if (r.intersects(o)) { ok=false; break; }
        if(!ok){ i--; continue; }
        carve_room(map, r);
        if(!rooms.empty()){
            int px = rooms.back().centerX(), py = rooms.back().centerY();
            int cx = r.centerX(), cy = r.centerY();
            if (rnd(0,1)==0){
                carve_h(map, px, cx, py);
                carve_v(map, py, cy, cx);
            } else {
                carve_v(map, py, cy, px);
                carve_h(map, px, cx, cy);
            }
        }
        rooms.push_back(r);
    }
    // fallback to open floor if none
    bool any=false;
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++) if(map[y][x]=='.') any=true;
    if(!any) for(int y=1;y<MAP_H-1;y++) for(int x=1;x<MAP_W-1;x++) map[y][x]='.';
}

// regenerate_map: builds map and places player, enemies and items according to difficulty
void regenerate_map(Grid &map, vector<Rect> &rooms, int &playerX, int &playerY,
                    vector<Enemy> &enemies, vector<Item> &items, Difficulty diff,
                    int &playerMaxHP, int &playerAttack, int &enemyAttackDamage, int &potionHeal, int &score) {
    // generate map
    generate_map_basic(map, rooms);

    // place player at center of first room or random floor
    if(!rooms.empty()){ playerX = rooms[0].centerX(); playerY = rooms[0].centerY(); }
    else { auto p = random_floor_tile(map); playerX=p.first; playerY=p.second; }

    // difficulty config lookup
    DiffConfig cfg = diffConfigs[(int)diff];

    // create enemies
    enemies.clear();
    int ecount = rnd(cfg.enemyMin, cfg.enemyMax);
    for(int i=0;i<ecount;i++){
        auto p = random_floor_tile(map);
        if (p.first==playerX && p.second==playerY) { i--; continue; }
        bool clash=false;
        for(auto &e: enemies) if (e.x==p.first && e.y==p.second) clash=true;
        if(clash){ i--; continue; }
        Enemy en; en.x=p.first; en.y=p.second; en.hp = rnd(cfg.enemyHpMin, cfg.enemyHpMax); en.alive=true;
        enemies.push_back(en);
    }

    // items (potions)
    items.clear();
    int pcount = rnd(cfg.potionMin, cfg.potionMax);
    for(int i=0;i<pcount;i++){
        auto p = random_floor_tile(map);
        if (p.first==playerX && p.second==playerY) { i--; continue; }
        bool clash=false;
        for(auto &it: items) if(it.x==p.first && it.y==p.second) clash=true;
        for(auto &e: enemies) if(e.x==p.first && e.y==p.second) clash=true;
        if(clash){ i--; continue; }
        items.push_back({p.first,p.second});
    }

    // player stats: we set defaults here; caller may override
    playerMaxHP = 20;
    playerAttack = 4;
    // enemy attack damage choose base from config min..max for simplicity — we can take average
    enemyAttackDamage = rnd(cfg.enemyAtkMin, cfg.enemyAtkMax);
    potionHeal = 0; // unused; actual potion heal random 6-10 per pickup
    score = 0;
}

int enemy_index_at(const vector<Enemy>& enemies, int x,int y){
    for(size_t i=0;i<enemies.size();++i) if(enemies[i].alive && enemies[i].x==x && enemies[i].y==y) return (int)i;
    return -1;
}
int item_index_at(const vector<Item>& items, int x,int y){
    for(size_t i=0;i<items.size();++i) if(items[i].x==x && items[i].y==y) return (int)i;
    return -1;
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // Setup difficulty configs
    diffConfigs[(int)EASY] = {2,4, 3,5, 1,2, 5,7};   // enemy count, hp range, atk range, potion count (potionMin/potionMax stored in last two fields)
    diffConfigs[(int)NORMAL] = {3,6, 4,8, 2,3, 3,5};
    diffConfigs[(int)HARD] = {5,8, 6,12, 3,5, 1,3};

    // Note: fields ordering in DiffConfig: enemyMin,enemyMax, enemyHpMin,enemyHpMax, enemyAtkMin,enemyAtkMax, potionMin,potionMax

    // Choose difficulty
    cout << "Choose difficulty: 1) Easy  2) Normal  3) Hard  : ";
    int dchoice = 2;
    if(!(cin>>dchoice)) return 0;
    Difficulty diff = NORMAL;
    if(dchoice==1) diff = EASY;
    else if(dchoice==3) diff = HARD;

    Grid map;
    vector<Rect> rooms;
    int playerX=1, playerY=1;
    vector<Enemy> enemies;
    vector<Item> items;
    int playerMaxHP=20, playerAttack=4, enemyAttackDamage=2, potionHeal=8;
    int score = 0;

    // regenerate map according to difficulty (initial placement)
    regenerate_map(map, rooms, playerX, playerY, enemies, items, diff, playerMaxHP, playerAttack, enemyAttackDamage, potionHeal, score);

    int playerHP = playerMaxHP;
    int turns = 0;
    const string hsFile = "highscore.txt";
    int highScore = load_high_score(hsFile);

    // main loop
    while(true){
        render(map, enemies, items, playerX, playerY, playerHP, playerMaxHP, score, turns, highScore, diff);
        if(playerHP <= 0){
            cout << "You died! Final score: " << score << "   Turns: " << turns << "\n";
            if (score > highScore) {
                cout << "New high score!\n";
                save_high_score(hsFile, score);
            } else {
                cout << "High score: " << highScore << "\n";
            }
            break;
        }
        cout << "Enter move (w/a/s/d) or q to quit: ";
        char ch; if(!(cin>>ch)) break;
        if(ch=='q' || ch=='Q'){
            cout << "Quitting. Final score: " << score << "\n";
            if (score > highScore) {
                cout << "New high score!\n";
                save_high_score(hsFile, score);
            }
            break;
        }
        int nx = playerX, ny = playerY;
        if(ch=='w' || ch=='W') ny--;
        else if(ch=='s' || ch=='S') ny++;
        else if(ch=='a' || ch=='A') nx--;
        else if(ch=='d' || ch=='D') nx++;
        else {
            cout << "Unknown input. Use w/a/s/d.\n";
            continue;
        }
        if(nx<0||nx>=MAP_W||ny<0||ny>=MAP_H){
            cout << "Cannot move out of bounds.\n";
            continue;
        }
        if(map[ny][nx] == '#'){
            cout << "Bumped into a wall.\n";
            // count as a turn; enemies still take their turn
            turns++;
        } else {
            // Is there an enemy at destination?
            int eidx = enemy_index_at(enemies, nx, ny);
            if(eidx != -1){
                // attack enemy
                cout << "You attack the enemy for " << playerAttack << " damage!\n";
                enemies[eidx].hp -= playerAttack;
                if(enemies[eidx].hp <= 0){
                    cout << "Enemy defeated! +10 score.\n";
                    enemies[eidx].alive = false;
                    score += 10; // new scoring: +10 per kill
                    // move player into tile of dead enemy
                    playerX = nx; playerY = ny;
                } else {
                    cout << "Enemy HP left: " << enemies[eidx].hp << "\n";
                    // player stays in place after attacking
                }
                turns++;
            } else {
                // Is there an item there?
                int itidx = item_index_at(items, nx, ny);
                if(itidx != -1){
                    int heal = rnd(6,10); // potions heal 6-10
                    int before = playerHP;
                    playerHP = min(playerMaxHP, playerHP + heal);
                    cout << "Picked up a potion! Healed " << (playerHP - before) << " HP (+" << heal << " roll, capped).\n";
                    // remove item
                    items.erase(items.begin() + itidx);
                }
                // move player
                playerX = nx; playerY = ny;
                turns++;
            }
        }

        // Enemy turn: each enemy computes a next step via BFS avoiding walls and other enemies.
        // We must consider simultaneous movement without stacking: we compute intended moves, then resolve.
        int enemyDamageThisTurn = 0; // track damage inflicted to player (can be multiple enemies)
        vector<pair<int,int>> occupied; // positions occupied at movement planning (start positions)
        for(auto &e: enemies) if(e.alive) occupied.emplace_back(e.x,e.y);
        // We'll build target positions and then validate to avoid collisions; ties resolved by order.
        vector<pair<int,int>> nextPos(enemies.size());
        for(size_t i=0;i<enemies.size();++i){
            if(!enemies[i].alive){ nextPos[i] = {-1,-1}; continue; }
            // build occupied list for this BFS excluding self
            vector<pair<int,int>> occ;
            for(size_t j=0;j<enemies.size();++j) if(j!=i && enemies[j].alive) occ.emplace_back(enemies[j].x, enemies[j].y);
            pair<int,int> step = bfs_next_step(map, enemies[i].x, enemies[i].y, playerX, playerY, occ);
            nextPos[i] = step;
        }
        // resolve moves in order, forbidding stepping onto tiles that another earlier-moving enemy already took (except player tile).
        set<pair<int,int>> reserved; // reserved tiles (by earlier moves)
        for(size_t i=0;i<enemies.size();++i){
            if(!enemies[i].alive) continue;
            auto intended = nextPos[i];
            // if intended is player's tile, attack
            if(intended.first==playerX && intended.second==playerY){
                // enemy attacks player
                int edmg = rnd(diffConfigs[(int)diff].enemyAtkMin, diffConfigs[(int)diff].enemyAtkMax);
                cout << "An enemy attacks you for " << edmg << " damage!\n";
                playerHP -= edmg;
                // enemy doesn't move into player's tile permanently (stays adjacent), but based on earlier spec enemy could step into player tile and attack.
                // We'll keep enemy at original position if stepping onto player (common roguelike behavior is enemy moves in and attacks; here: remain or move? We'll keep them where they are.)
                reserved.insert({enemies[i].x, enemies[i].y});
            } else {
                // ensure intended tile is not reserved and is floor and not occupied by other alive enemy after resolution
                bool blocked=false;
                if(intended.first<0) blocked=true;
                if(!blocked){
                    if(map[intended.second][intended.first] != '.') blocked=true;
                    if(reserved.count(intended)) blocked=true;
                    // also ensure not stepping onto another alive enemy's current tile unless that enemy is moving away (we check reserved and intended).
                }
                if(blocked){
                    // stay in place
                    reserved.insert({enemies[i].x, enemies[i].y});
                } else {
                    // move enemy
                    enemies[i].x = intended.first;
                    enemies[i].y = intended.second;
                    reserved.insert(intended);
                }
            }
        }

        // After enemies moved, check if any enemy occupies player's tile (if they moved into it) -> attack already handled above for intended==player tile.
        for(auto &e: enemies){
            if(!e.alive) continue;
            if(e.x==playerX && e.y==playerY){
                // If we reach here and player still alive, enemy deals damage (if not already applied)
                // To avoid double applying, we already applied damage when intended==player tile above; but if an enemy moved onto player in resolution, we attack now as well.
                // For safety, apply a small fixed damage if player shares tile:
                int edmg = rnd(diffConfigs[(int)diff].enemyAtkMin, diffConfigs[(int)diff].enemyAtkMax);
                cout << "An enemy hits you for " << edmg << " damage (bumped into you)!\n";
                playerHP -= edmg;
            }
        }

        // small cap
        if(playerHP > 999) playerHP = 999;
    }

    cout << "Thanks for playing!\n";
    return 0;
}
