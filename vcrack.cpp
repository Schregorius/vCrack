// =====================================================================
// vcrack.cpp  -  VeraCrypt (Nicht-System) Recovery, Alles-in-einem
//
//   Regeln in rules.txt (Laufzeit). Generator-Kern O(1)-ever, mehrkernig.
//   NEU: Live-Fortschrittsanzeige bei count / estimate / run
//        (Spinner, laufende Kandidatenzahl, Mio/s, Zeit, grobes %).
//   Ersetzt turbo_gen.exe UND RunTurbo.ps1.
//
// BAUEN:  build.bat
// BEFEHLE: rules | gen | count | bench | estimate | run
// OPTIONEN: --rules --words --target --min --max --skip --limit
//           --hash --size --device gpu|cpu|both --temp
// =====================================================================

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <cctype>
#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
  #define NOMINMAX
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define POPEN  _popen
  #define PCLOSE _pclose
  #define ISATTY(fd) _isatty(fd)
  #define FILENO(f)  _fileno(f)
#else
  #include <unistd.h>
  #include <sys/wait.h>
  #define POPEN  popen
  #define PCLOSE pclose
  #define ISATTY(fd) isatty(fd)
  #define FILENO(f)  fileno(f)
#endif
using namespace std;

// VT/ANSI + UTF-8 im Terminal aktivieren (Win10+), damit das DOS-Dashboard sauber laeuft
static void enableVT(){
#ifdef _WIN32
    HANDLE h=GetStdHandle(STD_ERROR_HANDLE); DWORD m=0;
    if(GetConsoleMode(h,&m)) SetConsoleMode(h,m|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    HANDLE ho=GetStdHandle(STD_OUTPUT_HANDLE); DWORD mo=0;
    if(GetConsoleMode(ho,&mo)) SetConsoleMode(ho,mo|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
#endif
}
static bool g_tty=false;   // Dashboard nur im echten Terminal; sonst schlichte Zeilen

static string lower(string s){ for(char&c:s) c=(char)tolower((unsigned char)c); return s; }
static string joinTok(const vector<string>&v){ string r; for(size_t i=0;i<v.size();i++){ if(i) r+=", "; r+=v[i]; } return r; }

// ------------------------------- Woerter -----------------------------
struct Word { string text; vector<unsigned char> bytes; bool isEver=false,isForever=false,isFor=false,is4=false; };
static vector<Word> W;  static vector<string> Wlower;  static int Nw=0;

// ------------------------------- Fortschritt -------------------------
static atomic<uint64_t> g_progress{0};   // bisher gezaehlte/gestreamte Kandidaten (grob)
static atomic<int>      g_branchDone{0};  // fertige Startwort-Zweige
static atomic<int>      g_branchTotal{0}; // Anzahl Startwort-Zweige gesamt
static atomic<bool>     g_monitorRun{false};

static string fmtCount(double c){
    char b[48];
    if(c>=1e9)      snprintf(b,sizeof(b),"%.3f Mrd.",c/1e9);
    else if(c>=1e6) snprintf(b,sizeof(b),"%.1f Mio.",c/1e6);
    else            snprintf(b,sizeof(b),"%.0f",c);
    return b;
}

// label: Beschriftung; animate: true -> \r-Zeile (count/estimate/gen),
//                                 false -> Zeilenweise alle periodMs (run, neben hashcat).
static void monitor(const char* label, bool animate, int periodMs){
    const char spin[] = {'|','/','-','\\'}; int s=0;
    auto t0 = chrono::steady_clock::now();
    while(g_monitorRun.load()){
        this_thread::sleep_for(chrono::milliseconds(animate?200:periodMs));
        if(!g_monitorRun.load()) break;
        double el = chrono::duration<double>(chrono::steady_clock::now()-t0).count();
        double c  = (double)g_progress.load();
        int bd=g_branchDone.load(), bt=g_branchTotal.load();
        double pct  = bt>0 ? 100.0*bd/bt : 0.0;
        double rate = el>0 ? c/el/1e6 : 0.0;
        if(animate){
            char line[160];
            snprintf(line,sizeof(line)," %c %s  ~%.0f%%  |  %s Kandidaten  |  %.1f Mio/s  |  %.0fs",
                     spin[s++&3], label, pct, fmtCount(c).c_str(), rate, el);
            // auf feste Breite mit Leerzeichen auffuellen, damit Reste geloescht werden
            int len=(int)strlen(line); for(int k=len;k<120;k++) line[k]=' '; line[120]=0;
            fprintf(stderr,"\r%s",line); fflush(stderr);
        } else {
            fprintf(stderr,"    [gen] %s Kandidaten erzeugt  |  %.1f Mio/s  |  ~%.0f%% der Startwoerter  |  %.0fs\n",
                     fmtCount(c).c_str(), rate, pct, el); fflush(stderr);
        }
    }
    if(animate){ char sp[122]; for(int k=0;k<120;k++) sp[k]=' '; sp[120]=0; fprintf(stderr,"\r%s\r",sp); fflush(stderr); }
}

// ------------------------------- Regeln ------------------------------
struct Rule { string type; int a=0,b=0; vector<string> left,right; };
static vector<Rule> RULES;

static uint64_t forbiddenNext[64];
static int      wordMinPos[64];
static vector<uint64_t> exclusiveGroups;
struct TailRule { int n; uint64_t mask; };
static vector<TailRule> tailRules;
static bool maxOneEver=false;
static int ruleMin=7, ruleMax=10;

static uint64_t maskOf(const vector<string>&toks){
    uint64_t m=0;
    for(const string&t:toks){ string lt=lower(t); bool hit=false;
        for(int k=0;k<Nw;k++) if(Wlower[k]==lt){ m|=(1ull<<k); hit=true; }
        if(!hit) fprintf(stderr,"[!] Regel-Wort nicht in Liste: %s\n",t.c_str()); }
    return m;
}
static void compileRules(){
    for(int i=0;i<64;i++){ forbiddenNext[i]=0; wordMinPos[i]=1; }
    exclusiveGroups.clear(); tailRules.clear(); maxOneEver=false; ruleMin=7; ruleMax=10;
    for(const Rule&r:RULES){
        if(r.type=="laenge"){ ruleMin=r.a; ruleMax=r.b; }
        else if(r.type=="abpos"){ uint64_t m=maskOf(r.left);
            for(int k=0;k<Nw;k++) if((m>>k)&1) if(r.a>wordMinPos[k]) wordMinPos[k]=r.a; }
        else if(r.type=="nachbarn"){ uint64_t lm=maskOf(r.left), rm=maskOf(r.right);
            for(int k=0;k<Nw;k++) if((lm>>k)&1) forbiddenNext[k]|=rm; }
        else if(r.type=="exklusiv"){ exclusiveGroups.push_back(maskOf(r.left)); }
        else if(r.type=="nicht-am-ende"){ tailRules.push_back({r.a, maskOf(r.left)}); }
        else if(r.type=="max-ein-ever"){ maxOneEver=true; }
    }
}
// Eine einzelne Regelzeile parsen und (falls gueltig) an RULES anhaengen.
// Genutzt von loadRulesFile (rules.txt) UND loadVcFile (.vc [rules]).
static void parseRuleLine(string line){
    size_t h=line.find('#'); if(h!=string::npos) line=line.substr(0,h);
    stringstream ss(line); vector<string> tok; string t; while(ss>>t) tok.push_back(t);
    if(tok.empty()) return; string kw=lower(tok[0]); Rule r;
    // English keywords are primary; German keywords kept as aliases (backward compatible).
    if((kw=="length"||kw=="laenge") && tok.size()>=3){ r.type="laenge"; r.a=atoi(tok[1].c_str()); r.b=atoi(tok[2].c_str()); }
    else if((kw=="frompos"||kw=="abpos") && tok.size()>=2){ r.type="abpos"; r.a=atoi(tok[1].c_str());
        for(size_t i=2;i<tok.size();i++){ if(tok[i]==":") continue; r.left.push_back(tok[i]); } }
    else if(kw=="forbid-adjacent"||kw=="nachbarn-verboten"){ r.type="nachbarn"; bool right=false;
        for(size_t i=1;i<tok.size();i++){ if(tok[i]=="->"){ right=true; continue; }
            if(right) r.right.push_back(tok[i]); else r.left.push_back(tok[i]); } }
    else if(kw=="exclusive"||kw=="exklusiv"){ r.type="exklusiv"; for(size_t i=1;i<tok.size();i++) r.left.push_back(tok[i]); }
    else if((kw=="not-at-end"||kw=="nicht-am-ende") && tok.size()>=2){ r.type="nicht-am-ende"; r.a=atoi(tok[1].c_str());
        for(size_t i=2;i<tok.size();i++){ if(tok[i]==":") continue; r.left.push_back(tok[i]); } }
    else if(kw=="max-one-ever"||kw=="max-ein-ever"){ r.type="max-ein-ever"; }
    else { fprintf(stderr,"[!] Unknown rule: %s\n",tok[0].c_str()); return; }
    RULES.push_back(r);
}
static bool loadRulesFile(const string&file){
    ifstream in(file); if(!in) return false; RULES.clear(); string line;
    while(getline(in,line)) parseRuleLine(line);
    return true;
}
static void installDefaultRules(){
    RULES.clear(); auto R=[&](Rule r){ RULES.push_back(r); };
    R({"laenge",7,10,{},{}});
    R({"abpos",4,0,{"4","!"},{}});
    R({"abpos",3,0,{"For"},{}});
    R({"nachbarn",0,0,{"I","It","Ever","Never","Miss","4","!","For","Without","WithOut","Will","This","Me"},{"Patricia"}});
    R({"nachbarn",0,0,{"Patricia"},{"Will","Miss","Always","Me"}});
    R({"nachbarn",0,0,{"Without","WithOut","Will","Love"},{"I"}});
    R({"nachbarn",0,0,{"Miss","Fuck","Without","WithOut","Life"},{"Miss","Fuck","Without","WithOut","Life"}});
    R({"nachbarn",0,0,{"It","Me","I"},{"It","Me","I"}});
    R({"nachbarn",0,0,{"Miss","4","For","Fuck","Life"},{"Forever","Ever"}});
    R({"nachbarn",0,0,{"Forever","Ever"},{"Miss","4","For","Fuck","Life"}});
    R({"exklusiv",0,0,{"Without","WithOut"},{}});
    R({"nicht-am-ende",3,0,{"Patricia","Love","Fuck"},{}});
    R({"max-ein-ever",0,0,{},{}});
}
static void printRules(bool fromFile){
    fprintf(stderr,"[*] Active rules (%s):\n\n", fromFile? "from rules.txt":"built-in default");
    for(const Rule&r:RULES){
        if(r.type=="laenge")        printf("  length: %d to %d words\n",r.a,r.b);
        else if(r.type=="abpos")    printf("  only from position %d: %s\n",r.a,joinTok(r.left).c_str());
        else if(r.type=="nachbarn") printf("  forbidden adjacent: {%s} -> {%s}\n",joinTok(r.left).c_str(),joinTok(r.right).c_str());
        else if(r.type=="exklusiv") printf("  at most one of: %s\n",joinTok(r.left).c_str());
        else if(r.type=="nicht-am-ende") printf("  not in the last %d words: %s\n",r.a,joinTok(r.left).c_str());
        else if(r.type=="max-ein-ever")  printf("  at most one 'ever' concept\n");
    }
}

// ------------------------------- Woerter laden -----------------------
static bool loadWords(const string&file){
    ifstream in(file); if(!in) return false; string line;
    while(getline(in,line)){
        size_t a=0,b=line.size();
        while(a<b&&(unsigned char)line[a]<=' ') a++;
        while(b>a&&(unsigned char)line[b-1]<=' ') b--;
        if(b<=a) continue; string t=line.substr(a,b-a);
        Word w; w.text=t; w.bytes.assign(t.begin(),t.end()); string lw=lower(t);
        w.isEver=(lw=="ever"); w.isForever=(lw=="forever"); w.isFor=(lw=="for"); w.is4=(lw=="4");
        W.push_back(std::move(w)); Wlower.push_back(lw);
    }
    return true;
}

// ------------------------------- Optionen ----------------------------
static string wordsFile="list.txt",targetFile="Fvc_header.tc",rulesFile="rules.txt";
static string vcFile="";        // --vc: .vc als einzige Quelle (Woerter+Regeln+Header+Fortschritt)
static bool   targetSet=false;  // wurde --target explizit gesetzt?

// Laedt Woerter, Regeln und Header aus einer .vc-Datei (INI). Fuellt W/Wlower und RULES.
// Der [progress]-Block wird NICHT hier gelesen (das macht loadState gegen dieselbe Datei).
static bool loadVcFile(const string&path){
    ifstream in(path); if(!in) return false;
    W.clear(); Wlower.clear(); RULES.clear();
    string line, section;
    vector<pair<int,string>> words;   // (index, text)
    while(getline(in,line)){
        // CR entfernen (falls CRLF)
        if(!line.empty() && line.back()=='\r') line.pop_back();
        string s=line; size_t a=0,b=s.size();
        while(a<b&&(unsigned char)s[a]<=' ') a++; while(b>a&&(unsigned char)s[b-1]<=' ') b--;
        s=s.substr(a,b-a);
        if(s.empty()||s[0]=='#') continue;
        if(s[0]=='['&&s.back()==']'){ section=lower(s.substr(1,s.size()-2)); continue; }
        if(section=="words"){
            // "index ; used ; text"
            vector<string> p; string cur; for(char c:s){ if(c==';'){p.push_back(cur);cur.clear();} else cur+=c; } p.push_back(cur);
            if(p.size()>=3){
                int idx=atoi(p[0].c_str());
                string txt=p[2];
                size_t x=0,y=txt.size(); while(x<y&&(unsigned char)txt[x]<=' ')x++; while(y>x&&(unsigned char)txt[y-1]<=' ')y--;
                txt=txt.substr(x,y-x);
                if(!txt.empty()) words.push_back({idx,txt});
            }
        } else if(section=="rules"){
            parseRuleLine(s);
        } else if(section=="header"){
            size_t eq=s.find('='); if(eq!=string::npos){
                string k=lower(s.substr(0,eq)); string v=s.substr(eq+1);
                size_t x=0,y=v.size(); while(x<y&&(unsigned char)v[x]<=' ')x++; while(y>x&&(unsigned char)v[y-1]<=' ')y--;
                v=v.substr(x,y-x);
                if(k.find("file")!=string::npos && !targetSet && !v.empty()) targetFile=v;
            }
        }
    }
    std::sort(words.begin(),words.end(),[](const pair<int,string>&A,const pair<int,string>&B){return A.first<B.first;});
    for(auto&pr:words){
        const string&t=pr.second;
        Word w; w.text=t; w.bytes.assign(t.begin(),t.end()); string lw=lower(t);
        w.isEver=(lw=="ever"); w.isForever=(lw=="forever"); w.isFor=(lw=="for"); w.is4=(lw=="4");
        W.push_back(std::move(w)); Wlower.push_back(lw);
    }
    return !W.empty();
}
static string fHash="",fSize="",device="gpu"; static int tempAbort=90; static int cliMin=-1,cliMax=-1;
static int gMinLen=7,gMaxLen=10;
static uint64_t gSkip=0,gLimit=UINT64_MAX;
static int gSamples=300000;
static bool gRebench=false;
static int gCheckpointSec=300;   // Zwischenspeicherung spaetestens alle N Sekunden (0 = aus)
static bool gJson=false;         // Maschinen-Ausgabe (fuer die GUI)
static int gWorkload=3;          // hashcat -w (1..4); hoeher = mehr GPU, weniger Desktop-Reaktion
static string gSrc="";           // Quelle fuer 'header'-Befehl (VeraCrypt-Volume/Containerdatei)
static long gHeaderBytes=524288; // Bytes fuer Header-Extraktion (512 KB deckt Standard + versteckte Header ab)
static const int SHARD2_MINLEN=9;// ab dieser Laenge wird je Block zusaetzlich nach dem 2. Wort aufgeteilt (feinere Fortschrittsanzeige)

// ------------------------------- Signatur / Such-Hash / Cache --------
static string toHex(size_t h){ char b[20]; snprintf(b,sizeof(b),"%016zx",(size_t)h); return b; }
static string rulesSignature(){
    vector<string> parts;
    for(const Rule&r:RULES){
        vector<string> L=r.left,R=r.right;
        for(auto&x:L) x=lower(x); for(auto&x:R) x=lower(x);
        sort(L.begin(),L.end()); sort(R.begin(),R.end());
        parts.push_back(r.type+":"+to_string(r.a)+":"+to_string(r.b)+":"+joinTok(L)+"|"+joinTok(R));
    }
    sort(parts.begin(),parts.end());
    string all; for(auto&p:parts){ all+=p; all+="\n"; }
    return toHex(std::hash<string>{}(all));
}
static string wordsCsvCur(){ vector<string> v; for(auto&w:W) v.push_back(w.text); sort(v.begin(),v.end()); string s; for(size_t i=0;i<v.size();i++){ if(i)s+=","; s+=v[i]; } return s; }
static unordered_set<string> csvToSet(const string&csv){ unordered_set<string> s; string cur; for(char c:csv){ if(c==','){ if(!cur.empty())s.insert(cur); cur.clear(); } else cur+=c; } if(!cur.empty()) s.insert(cur); return s; }
static bool subsetOf(const unordered_set<string>&a,const unordered_set<string>&b){ if(a.size()>b.size()) return false; for(auto&x:a) if(!b.count(x)) return false; return true; }

// ---- Regel-Serialisierung fuer Resume (pipe-frei, spaeter re-evaluierbar) ----
// Jeder erledigte Block speichert NEBEN dem Hash seine komplette Regelmenge als
// Text. Dadurch kann vcrack fuer jeden aktuellen Kandidaten exakt pruefen, ob ein
// frueher erledigter Block ihn bereits erzeugt (und damit getestet) hat - egal ob
// sich seither Woerter ODER Regeln geaendert haben. Nichts wird je doppelt getestet,
// und eine Regelaenderung verwirft keinen Fortschritt mehr.
// Format je Regel:  type~a~b~leftcsv~rightcsv   (Regeln mit ';' getrennt).
static string ruleWordsCsv(vector<string> v){ for(auto&x:v)x=lower(x); sort(v.begin(),v.end());
    string s; for(size_t i=0;i<v.size();i++){ if(i)s+=","; s+=v[i]; } return s; }
static string rulesSerialize(){
    string out;
    for(const Rule&r:RULES){
        if(!out.empty()) out+=";";
        out+=r.type+"~"+to_string(r.a)+"~"+to_string(r.b)+"~"+ruleWordsCsv(r.left)+"~"+ruleWordsCsv(r.right);
    }
    return out.empty()? string("-") : out;   // "-" markiert leere Regelmenge (nicht "kein Eintrag")
}
static vector<string> splitCh(const string&s,char d){ vector<string> f; string c;
    for(char ch:s){ if(ch==d){ f.push_back(c); c.clear(); } else c+=ch; } f.push_back(c); return f; }
struct CoverRule{ string type; int a=0,b=0; vector<string> left,right; };
static vector<CoverRule> rulesParse(const string&blob){
    vector<CoverRule> rs; if(blob.empty()||blob=="-") return rs;
    for(const string&part:splitCh(blob,';')){ if(part.empty()) continue;
        vector<string> f=splitCh(part,'~'); if(f.size()<5) continue;
        CoverRule r; r.type=f[0]; r.a=atoi(f[1].c_str()); r.b=atoi(f[2].c_str());
        if(!f[3].empty()) r.left=splitCh(f[3],','); if(!f[4].empty()) r.right=splitCh(f[4],',');
        rs.push_back(r); }
    return rs;
}
// Vorkompilierte Alt-Regelmenge eines erledigten Blocks, ausgewertet gegen die
// AKTUELLE Wortliste. covers() beantwortet: "Hat dieser (jetzt gueltige) Kandidat
// schon im alten Block existiert?" -> dann ueberspringen.
struct CompiledCover{
    uint64_t wordset=0;              // welche AKTUELLEN Woerter lagen im Alt-Wortset
    uint64_t forbiddenNext[64];
    int      minPos[64];
    vector<uint64_t> excl;
    vector<TailRule> tails;
    bool maxOneEver=false;
    void build(const vector<CoverRule>&rules,const unordered_set<string>&oldWords){
        for(int i=0;i<64;i++){ forbiddenNext[i]=0; minPos[i]=1; }
        excl.clear(); tails.clear(); maxOneEver=false; wordset=0;
        for(int k=0;k<Nw;k++) if(oldWords.count(W[k].text)) wordset|=(1ull<<k);
        auto mask=[&](const vector<string>&toks)->uint64_t{ uint64_t m=0;
            for(const string&t:toks){ string lt=lower(t);
                for(int k=0;k<Nw;k++) if(Wlower[k]==lt) m|=(1ull<<k); } return m; };
        for(const CoverRule&r:rules){
            if(r.type=="abpos"){ uint64_t m=mask(r.left);
                for(int k=0;k<Nw;k++) if((m>>k)&1) if(r.a>minPos[k]) minPos[k]=r.a; }
            else if(r.type=="nachbarn"){ uint64_t lm=mask(r.left),rm=mask(r.right);
                for(int k=0;k<Nw;k++) if((lm>>k)&1) forbiddenNext[k]|=rm; }
            else if(r.type=="exklusiv"){ excl.push_back(mask(r.left)); }
            else if(r.type=="nicht-am-ende"){ tails.push_back({r.a,mask(r.left)}); }
            else if(r.type=="max-ein-ever"){ maxOneEver=true; }
            // "laenge" ist pro Block fix -> hier ohne Belang
        }
    }
    // War der Kandidat path[0..len-1] (aktuelle Indizes) unter DIESEN Alt-Regeln gueltig?
    bool covers(const int*path,int len) const{
        uint64_t um=0;
        for(int d=0; d<len; d++){ int i=path[d];
            if(!((wordset>>i)&1)) return false;                        // Wort war nicht im Alt-Wortset
            if(d+1<minPos[i]) return false;                            // Positionsregel (abpos)
            if(d>0 && ((forbiddenNext[path[d-1]]>>i)&1)) return false;  // verbotener Nachbar
            for(uint64_t g:excl){ if(((g>>i)&1)&&(um&g)) return false; } // exklusiv
            um|=(1ull<<i);
        }
        for(const TailRule&tr:tails)
            for(int k=len-tr.n;k<len;k++){ if(k<0) continue; if((tr.mask>>path[k])&1) return false; }
        if(maxOneEver){
            int everIso=0,everForever=0,everForEver=0,ever4Ever=0,everPart=0;
            for(int d=0; d<len; d++){ int i=path[d];
                if(W[i].isEver) everIso++;
                if(W[i].isForever) everForever++;
                if(d>0){ const Word&last=W[path[d-1]];
                    if((last.isFor||last.is4)&&W[i].isEver){ everPart++; if(last.isFor) everForEver++; else ever4Ever++; } } }
            int ec=(everForever>0)+(everForEver>0)+(ever4Ever>0)+((everIso-everPart)>0);
            if(ec>1) return false;
        }
        return true;
    }
};

// Such-Hash = Quersumme aus ALLEN keyspace-relevanten Einstellungen (Woerter + Regeln + Laengen).
static string searchHash(int mn,int mx){
    string s = wordsCsvCur() + "#" + rulesSignature() + "#" + to_string(mn) + "-" + to_string(mx);
    return toHex(std::hash<string>{}(s));
}
static const char* CACHE_FILE="vcrack_cache.txt";
static bool cacheGet(const string&h,const char*kind,double&outVal){
    ifstream in(CACHE_FILE); if(!in) return false; string line;
    string want=string("KS|")+h+"|"+kind+"|"; bool found=false; double v=0;
    while(getline(in,line)){ if(line.rfind(want,0)==0){ v=atof(line.c_str()+want.size()); found=true; } }
    if(found) outVal=v; return found;
}
static void cachePut(const string&h,const char*kind,double val){
    FILE*f=fopen(CACHE_FILE,"ab"); if(!f) return;
    fprintf(f,"KS|%s|%s|%.0f\n",h.c_str(),kind,val); fclose(f);
}
// Bench-Cache: H/s je (Device, Hash). Haengt NUR an GPU+Hash, nicht am Keyspace.
static const char* BENCH_FILE="vcrack_bench.txt";
static bool benchCacheGet(const string&dev,const string&hashName,double&out){
    ifstream in(BENCH_FILE); if(!in) return false; string line;
    string want=string("BENCH|")+dev+"|"+hashName+"|"; bool found=false; double v=0;
    while(getline(in,line)){ if(line.rfind(want,0)==0){ v=atof(line.c_str()+want.size()); found=true; } }
    if(found) out=v; return found;
}
static void benchCachePut(const string&dev,const string&hashName,double val){
    FILE*f=fopen(BENCH_FILE,"ab"); if(!f) return;
    fprintf(f,"BENCH|%s|%s|%.0f\n",dev.c_str(),hashName.c_str(),val); fclose(f);
}

// ------------------------------- Worker ------------------------------
struct Worker {
    vector<char> used; vector<int> path; vector<unsigned char> buf; uint64_t usedMask=0;
    int everIso=0,everPart=0,everForever=0,everForEver=0,ever4Ever=0;
    uint64_t total=0,produced=0,outCount=0,skipN=0,limitN=UINT64_MAX,tick=0;
    vector<const CompiledCover*> coverSets; // bereits erledigte Konfigs dieses Blocks: deren Kandidaten ueberspringen
    bool done=false,countOnly=false;
    FILE* out=nullptr; vector<unsigned char> obuf;
    int minLen=7,maxLen=10;
    vector<int> firstWords;
    int secondWord=-1;        // wenn >=0: Tiefe 1 nur dieses Wort (feinere Block-Aufteilung bei langen Laengen)

    void init(bool count,int mn,int mx){
        used.assign(Nw,0); path.assign(Nw,-1); buf.clear(); usedMask=0;
        everIso=everPart=everForever=everForEver=ever4Ever=0;
        total=produced=outCount=0; tick=0; done=false; countOnly=count; coverSets.clear();
        obuf.clear(); obuf.reserve((16u<<20)+1024); buf.reserve(256);
        minLen=mn; maxLen=mx; secondWord=-1;
    }
    inline void flushOut(){ if(!obuf.empty()&&out){ fwrite(obuf.data(),1,obuf.size(),out); obuf.clear(); } }
    inline void progressTick(){ if(((++tick) & ((1u<<20)-1))==0) g_progress.fetch_add(1u<<20, memory_order_relaxed); }
    inline void emit(int depth){
        for(const CompiledCover* c:coverSets) if(c->covers(path.data(),depth)) return; // schon getestet -> ueberspringen
        if(countOnly){ total++; progressTick(); return; }
        if(produced<skipN){ produced++; return; }
        if(outCount>=limitN){ done=true; return; }
        obuf.insert(obuf.end(),buf.begin(),buf.end()); obuf.push_back('\n');
        if(obuf.size()>=(16u<<20)) flushOut();
        produced++; outCount++; progressTick(); if(outCount>=limitN) done=true;
    }
    inline bool emitAllowed(int d){
        for(const TailRule&tr:tailRules)
            for(int k=d-tr.n;k<d;k++){ if(k<0) continue; if((tr.mask>>path[k])&1) return false; }
        return true;
    }
    inline bool validEver(int d,int i){
        int tiso=everIso+(W[i].isEver?1:0);
        int tforever=everForever+(W[i].isForever?1:0);
        int tpart=everPart,tForEver=everForEver,t4Ever=ever4Ever;
        if(d>0){ const Word&last=W[path[d-1]];
            if((last.isFor||last.is4)&&W[i].isEver){ tpart++; if(last.isFor) tForEver++; else t4Ever++; } }
        int ec=(tforever>0)+(tForEver>0)+(t4Ever>0)+((tiso-tpart)>0);
        return ec<=1;
    }
    void gen(int depth){
        if(done) return;
        if(depth>=minLen&&depth<=maxLen){ if(emitAllowed(depth)) emit(depth); if(done) return; }
        if(depth>=maxLen) return;
        if(depth==0){ for(int i:firstWords){ step(depth,i); g_branchDone.fetch_add(1,memory_order_relaxed); if(done) return; } }
        else if(depth==1 && secondWord>=0){ if(!used[secondWord]) step(depth,secondWord); }
        else { for(int i=0;i<Nw;i++){ if(used[i]) continue; step(depth,i); if(done) return; } }
    }
    inline void step(int depth,int i){
        int pos=depth+1;
        if(pos<wordMinPos[i]) return;
        if(maxOneEver && !validEver(depth,i)) return;
        if(depth>0){ int last=path[depth-1]; if((forbiddenNext[last]>>i)&1) return; }
        for(uint64_t g:exclusiveGroups){ if(((g>>i)&1)&&(usedMask&g)) return; }
        used[i]=1; usedMask|=(1ull<<i); size_t ob=buf.size();
        if(!countOnly){ buf.insert(buf.end(),W[i].bytes.begin(),W[i].bytes.end()); }
        path[depth]=i;
        int dIso=0,dPart=0,dForever=0,dForEver=0,d4Ever=0;
        if(W[i].isEver){ everIso++; dIso=1; }
        if(W[i].isForever){ everForever++; dForever=1; }
        if(depth>0){ const Word&last=W[path[depth-1]];
            if((last.isFor||last.is4)&&W[i].isEver){ everPart++; dPart=1; if(last.isFor){ everForEver++; dForEver=1; } else { ever4Ever++; d4Ever=1; } } }
        gen(depth+1);
        everIso-=dIso; everForever-=dForever; everPart-=dPart; everForEver-=dForEver; ever4Ever-=d4Ever;
        if(!countOnly) buf.resize(ob);
        used[i]=0; usedMask&=~(1ull<<i);
    }
};

// Mehrkerniges Zaehlen mit Live-Anzeige.
static uint64_t parallelCount(int mn,int mx){
    unsigned T=thread::hardware_concurrency(); if(!T) T=4; if((int)T>Nw) T=(unsigned)Nw; if(T<1) T=1;
    vector<Worker> ws(T);
    for(unsigned t=0;t<T;t++){ ws[t].init(true,mn,mx); for(int i=(int)t;i<Nw;i+=(int)T) ws[t].firstWords.push_back(i); }
    g_progress=0; g_branchDone=0; g_branchTotal=Nw; g_monitorRun=true;
    thread mon(monitor,"Zaehle",true,200);
    vector<thread> th;
    for(unsigned t=0;t<T;t++) th.emplace_back([&ws,t]{ ws[t].gen(0); });
    for(auto&x:th) x.join();
    g_monitorRun=false; mon.join();
    uint64_t sum=0; for(auto&w:ws) sum+=w.total; return sum;
}

// ------------------------------- Modi --------------------------------
struct Mode { const char*code; const char*hash; const char*size; const char*cipher; };
static const Mode MODES[] = {
    {"13721","SHA-512",  "512","Einzel-Cipher"},{"13722","SHA-512",  "1024","2er-Kaskade"},{"13723","SHA-512",  "1536","3er-Kaskade"},
    {"13731","Whirlpool","512","Einzel-Cipher"},{"13732","Whirlpool","1024","2er-Kaskade"},{"13733","Whirlpool","1536","3er-Kaskade"},
    {"13771","Streebog", "512","Einzel-Cipher"},{"13772","Streebog", "1024","2er-Kaskade"},{"13773","Streebog", "1536","3er-Kaskade"},
    {"13751","SHA-256",  "512","Einzel-Cipher"},{"13752","SHA-256",  "1024","2er-Kaskade"},{"13753","SHA-256",  "1536","3er-Kaskade"},
};
static const int NMODES=(int)(sizeof(MODES)/sizeof(MODES[0]));
static int devNum(){ if(device=="cpu") return 1; if(device=="both") return 3; return 2; }
static bool modeAllowed(const Mode&m){ if(!fHash.empty()&&fHash!=m.hash) return false; if(!fSize.empty()&&fSize!=m.size) return false; return true; }
static void humanEta(double s,char*o,size_t n){ if(s<=0){ snprintf(o,n,"n/a"); return; }
    double d=s/86400.0,h=s/3600.0,m=s/60.0;
    if(d>=1) snprintf(o,n,"%.1f Tage",d); else if(h>=1) snprintf(o,n,"%.1f Std",h);
    else if(m>=1) snprintf(o,n,"%.1f Min",m); else snprintf(o,n,"%.0f Sek",s); }
static double benchSpeed(const char*code){
    // Benchmark und die "Speed.#..:  N [kMG]H/s"-Zeile robust parsen (statt --machine-readable).
    char cmd[512]; snprintf(cmd,sizeof(cmd),".\\hashcat.exe -b -m %s -D %d -w %d",code,devNum(),gWorkload);
    FILE*p=POPEN(cmd,"r"); if(!p) return -1; char line[4096]; double best=0;
    while(fgets(line,sizeof(line),p)){
        char* s=strstr(line,"Speed."); if(!s) continue;
        char* c=strchr(s,':'); if(!c) continue;
        double num=0; char ub[16]={0};
        if(sscanf(c+1," %lf %15s",&num,ub)==2 && strstr(ub,"H/s")){
            double mult = ub[0]=='k'?1e3 : ub[0]=='M'?1e6 : ub[0]=='G'?1e9 : ub[0]=='T'?1e12 : 1.0;
            double val=num*mult; if(val>best) best=val;
        }
    }
    PCLOSE(p); return best>0?best:-1;
}

// ------------------------------- Aktionen ----------------------------
static double sampleKeyspace(int mn,int mx,int samples,double perLen[11],double& ciPct,bool dash); // vorwaerts
static string barOf(double frac,int w);   // vorwaerts (Definition im Dashboard-Abschnitt)

static int doGen(){
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    Worker wk; wk.init(false,gMinLen,gMaxLen); wk.out=stdout; wk.skipN=gSkip; wk.limitN=gLimit;
    for(int i=0;i<Nw;i++) wk.firstWords.push_back(i);
    g_progress=0; g_branchDone=0; g_branchTotal=Nw; g_monitorRun=true;
    thread mon(monitor,"Erzeuge",true,200);
    fprintf(stderr,"[*] %d Woerter, Laenge %d-%d ...\n",Nw,gMinLen,gMaxLen);
    wk.gen(0); wk.flushOut(); fflush(stdout);
    g_monitorRun=false; mon.join();
    fprintf(stderr,"[=] Ausgegeben: %llu\n",(unsigned long long)wk.outCount); return 0;
}
static int doCount(){
    string h=searchHash(gMinLen,gMaxLen); double cached;
    if(cacheGet(h,"exact",cached)){
        fprintf(stderr,"[=] Exakt aus Cache: %s Kandidaten (Such-Hash %s, unveraendert)\n",fmtCount(cached).c_str(),h.c_str());
        printf("%.0f\n",cached); return 0;
    }
    fprintf(stderr,"[*] Zaehle exakt (Laenge %d-%d, %u Threads) ...\n",gMinLen,gMaxLen,
            thread::hardware_concurrency()?thread::hardware_concurrency():1);
    uint64_t n=parallelCount(gMinLen,gMaxLen);
    cachePut(h,"exact",(double)n);
    printf("%llu\n",(unsigned long long)n);
    fprintf(stderr,"[=] Exakte Anzahl Kandidaten: %llu  (%s) - im Cache gespeichert (Such-Hash %s)\n",
            (unsigned long long)n,fmtCount((double)n).c_str(),h.c_str()); return 0;
}
// Liefert H/s fuer einen Hash (Device-abhaengig). Misst nur, wenn nicht im Cache.
// Begruendung: VeraCrypts Kosten stecken im PBKDF2 (Hash); die Cipher-Kaskade
// (512/1024/1536) aendert die H/s nur um Promille -> ein Messwert je Hash genuegt.
static double hsForHash(const char* hashName,const char* repCode){
    double v;
    if(!gRebench && benchCacheGet(device,hashName,v)) return v;
    v=benchSpeed(repCode);
    if(v>0) benchCachePut(device,hashName,v);
    return v;
}
static int doBench(){
    unordered_map<string,double> seen;
    fprintf(stderr,"[*] Benchmark je Hash (Device=%s)%s. Gilt fuer alle Cipher-Groessen (512/1024/1536).\n",
            device.c_str(), gRebench?" [neu messen]":"");
    for(int i=0;i<NMODES;i++){ if(!modeAllowed(MODES[i])) continue;
        if(seen.count(MODES[i].hash)) continue;
        fprintf(stderr,"--- %s  (-m %s) ...\n",MODES[i].hash,MODES[i].code);
        double v=hsForHash(MODES[i].hash,MODES[i].code); seen[MODES[i].hash]=v;
        if(v>0) fprintf(stderr,"    %s: %.0f H/s  (im Cache)\n",MODES[i].hash,v);
        else    fprintf(stderr,"    %s: Messung fehlgeschlagen\n",MODES[i].hash);
    }
    return 0;
}
static int doEstimate(){
    // estimate = sample (Keyspace + Verteilung) + bench (H/s) + ETA, in einem Schritt.
    string h=searchHash(gMinLen,gMaxLen);
    double perLen[11]; double ciPct=0;
    double sampTotal=sampleKeyspace(gMinLen,gMaxLen,gSamples,perLen,ciPct,!gJson);
    if(g_tty && !gJson) fprintf(stderr,"\n");
    double N=sampTotal; const char* src="Schaetzung";
    double exact;
    if(cacheGet(h,"exact",exact) && exact>0){
        double sc = sampTotal>0 ? exact/sampTotal : 1.0;   // Verteilung auf exakte Summe skalieren
        for(int d=0;d<11;d++) perLen[d]*=sc;
        N=exact; src="exakt (Cache)"; ciPct=0;
    } else {
        cachePut(h,"est",sampTotal);
    }
    if(gJson){
        unordered_map<string,double> hs;
        printf("{\"keyspace\":%.0f,\"ci_pct\":%.3f,\"min\":%d,\"max\":%d,\"device\":\"%s\",\"perlen\":{",
               N,ciPct,gMinLen,gMaxLen,device.c_str());
        bool pf=true;
        for(int d=gMinLen; d<=gMaxLen; d++){ printf("%s\"%d\":%.0f",pf?"":",",d,perLen[d]); pf=false; }
        printf("},\"modes\":[");
        bool first=true;
        for(int i=0;i<NMODES;i++){ if(!modeAllowed(MODES[i])) continue;
            if(!hs.count(MODES[i].hash)) hs[MODES[i].hash]=hsForHash(MODES[i].hash,MODES[i].code);
            double spd=hs[MODES[i].hash]; double sec=(spd>0)?N/spd:-1;
            printf("%s{\"code\":\"%s\",\"hash\":\"%s\",\"size\":\"%s\",\"cipher\":\"%s\",\"hs\":%.0f,\"eta_sec\":%.0f}",
                   first?"":",",MODES[i].code,MODES[i].hash,MODES[i].size,MODES[i].cipher,spd>0?spd:0,sec);
            first=false;
        }
        printf("]}\n"); return 0;
    }
    char cib[32]={0}; if(ciPct>0) snprintf(cib,sizeof(cib)," +/- %.1f%%",ciPct);
    fprintf(stderr,"[=] Keyspace: %s  (%s%s, Such-Hash %s)\n",
            fmtCount(N).c_str(),src,cib,h.c_str());
    // Verteilung je Laenge (ersetzt separaten 'sample'-Aufruf)
    printf("\n%-6s %16s   %s\n","Laenge","Kandidaten","Anteil");
    printf("--------------------------------------------------------------------\n");
    for(int d=gMinLen; d<=gMaxLen; d++){
        double frac = N>0 ? perLen[d]/N : 0;
        printf(" %-5d %16s   %s %5.1f%%\n",d,fmtCount(perLen[d]).c_str(),barOf(frac,20).c_str(),100*frac);
    }
    fflush(stdout);
    fprintf(stderr,"\n[*] H/s je Hash (Device=%s, aus Cache falls vorhanden) ...\n\n",device.c_str());
    printf("%-22s %-7s %18s  %-12s\n","Modus","Code","H/s","ETA");
    printf("--------------------------------------------------------------------\n");
    double tot=0; unordered_map<string,double> hs;
    for(int i=0;i<NMODES;i++){ if(!modeAllowed(MODES[i])) continue;
        if(!hs.count(MODES[i].hash)) hs[MODES[i].hash]=hsForHash(MODES[i].hash,MODES[i].code);
        double spd=hs[MODES[i].hash]; char lbl[48]; snprintf(lbl,sizeof(lbl),"%s/XTS-%s",MODES[i].hash,MODES[i].size);
        if(spd<=0){ printf("%-22s %-7s %18s  %-12s\n",lbl,MODES[i].code,"?","?"); continue; }
        double sec=N/spd; tot+=sec; char eta[32]; humanEta(sec,eta,sizeof(eta));
        printf("%-22s %-7s %18.0f  %-12s\n",lbl,MODES[i].code,spd,eta);
    }
    char t[32]; humanEta(tot,t,sizeof(t));
    printf("--------------------------------------------------------------------\n");
    printf("[=] Worst Case (alle Modi nacheinander): %s\n",t);
    printf("    Treffer beendet frueher; 13721 (SHA-512 Einzel) ist am wahrscheinlichsten.\n"); return 0;
}
// ------------------------------- Zustand / Resume --------------------
struct DoneRec{ string mode; int len; string fw; string rhash; string wcsv; string rules; };
static vector<DoneRec> g_state;
static unordered_map<string,uint64_t> g_part;   // Zwischenstand je Block -> Offset (getestete Kandidaten)
static const char* STATE_FILE="vcrack_state.txt";
static string g_stateFile=STATE_FILE;   // bei --vc: die .vc-Datei (Progress ist dort die letzte Sektion)
static vector<string> splitPipe(const string&s,size_t from){
    vector<string> f; string cur; for(size_t i=from;i<s.size();i++){ char c=s[i]; if(c=='|'){ f.push_back(cur); cur.clear(); } else cur+=c; } f.push_back(cur); return f;
}
// Schluessel eines Blocks inkl. cfgSig (Signatur der bereits erledigten Konfigs ->
// Zwischenstand nur fortsetzen, solange der Filterzustand derselbe ist)
static string partKey(const string&mode,int len,const string&fw,const string&rhash,const string&wcsv,uint64_t cfgSig){
    char rb[20]; snprintf(rb,sizeof(rb),"%016llx",(unsigned long long)cfgSig);
    return mode+"|"+to_string(len)+"|"+fw+"|"+rhash+"|"+wcsv+"|"+rb;
}
static void loadState(){
    g_state.clear(); g_part.clear(); ifstream in(g_stateFile); if(!in) return; string line;
    while(getline(in,line)){
        if(line.rfind("DONE|",0)==0){
            vector<string> f=splitPipe(line,5);
            if(f.size()<5) continue;
            // Feld 6 (Regel-Text) ist neu; Alt-Eintraege haben es nicht -> leer lassen.
            g_state.push_back(DoneRec{f[0],atoi(f[1].c_str()),f[2],f[3],f[4], f.size()>=6? f[5] : string()});
        } else if(line.rfind("PART|",0)==0){
            // PART|mode|len|fw|rhash|wcsv|reqMaskHex|offset
            vector<string> f=splitPipe(line,5);
            if(f.size()<7) continue;
            string key=f[0]+"|"+f[1]+"|"+f[2]+"|"+f[3]+"|"+f[4]+"|"+f[5];
            g_part[key]=strtoull(f[6].c_str(),nullptr,10);   // spaetere Zeile gewinnt
        }
    }
}
static void appendDone(const string&mode,int len,const string&fw,const string&rhash,const string&wcsv,const string&rules){
    FILE*f=fopen(g_stateFile.c_str(),"ab");
    if(f){ fprintf(f,"DONE|%s|%d|%s|%s|%s|%s\n",mode.c_str(),len,fw.c_str(),rhash.c_str(),wcsv.c_str(),rules.c_str()); fclose(f); }
    g_state.push_back(DoneRec{mode,len,fw,rhash,wcsv,rules});
}
static void writePart(const string&mode,int len,const string&fw,const string&rhash,const string&wcsv,uint64_t cfgSig,uint64_t offset){
    char rb[20]; snprintf(rb,sizeof(rb),"%016llx",(unsigned long long)cfgSig);
    FILE*f=fopen(g_stateFile.c_str(),"ab");
    if(f){ fprintf(f,"PART|%s|%d|%s|%s|%s|%s|%llu\n",mode.c_str(),len,fw.c_str(),rhash.c_str(),wcsv.c_str(),rb,(unsigned long long)offset); fclose(f); }
    g_part[partKey(mode,len,fw,rhash,wcsv,cfgSig)]=offset;
}

static int exitCodeOf(int rc){
#ifdef _WIN32
    return rc;
#else
    if(rc==-1) return -1;
    if(WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
#endif
}
// Prueft, ob hashcat ueberhaupt ein Rechendevice sieht (verhindert, dass leere
// Laeufe faelschlich als 'erledigt' gezaehlt werden).
static bool deviceCheck(){
    if(getenv("VCRACK_SKIP_DEVCHECK")) return true;
    char cmd[256]; snprintf(cmd,sizeof(cmd),".\\hashcat.exe -I");
    FILE*p=POPEN(cmd,"r"); if(!p) return false;
    char line[4096]; bool dev=false, nodev=false;
    while(fgets(line,sizeof(line),p)){
        if(strstr(line,"Device ID")||strstr(line,"Backend Device")) dev=true;
        if(strstr(line,"No devices")) nodev=true;
    }
    PCLOSE(p);
    return dev && !nodev;
}

static int doRun(){
    loadState();
    string rhash=rulesSignature();
    string curRules=rulesSerialize();
    unordered_set<string> curSet; for(auto&w:W) curSet.insert(w.text);
    string curCsv=wordsCsvCur();

    vector<int> fwList; for(int i=0;i<Nw;i++) if(wordMinPos[i]<=1) fwList.push_back(i); // nur moegliche Startwoerter
    vector<int> modeIdx; for(int i=0;i<NMODES;i++) if(modeAllowed(MODES[i])) modeIdx.push_back(i);

    // Feinere Aufteilung bei langen Laengen: zweites Wort je Startwort vorfiltern
    // (billige Checks; ungueltige Kombis wuerden sonst leere Bloecke erzeugen).
    auto exclConflict=[&](int a,int b)->bool{
        for(uint64_t g:exclusiveGroups) if(((g>>a)&1)&&((g>>b)&1)) return true; return false;
    };
    vector<vector<int>> swOf(Nw);
    for(int fw:fwList){
        for(int i=0;i<Nw;i++){
            if(i==fw) continue;
            if(wordMinPos[i]>2) continue;
            if((forbiddenNext[fw]>>i)&1) continue;
            if(exclConflict(fw,i)) continue;
            swOf[fw].push_back(i);
        }
    }
    auto sharded=[&](int len)->bool{ return len>=SHARD2_MINLEN; };

    long totalShards=0;
    for(int len=gMinLen; len<=gMaxLen; len++)
        for(int fwIdx:fwList)
            totalShards += (sharded(len) ? (long)swOf[fwIdx].size() : 1L);
    totalShards *= (long)modeIdx.size();
    long shard=0; int tested=0,deltas=0,skipped=0;

    if(!deviceCheck()){
        fprintf(stderr,"[-] hashcat findet KEIN Rechendevice (keine GPU sichtbar).\n");
        fprintf(stderr,"    Abbruch - es wird NICHTS getestet oder als erledigt markiert.\n");
        fprintf(stderr,"    Pruefe GPU-Treiber/CUDA, starte vcrack aus einer normalen Konsole, dann erneut.\n");
        return 2;
    }

    fprintf(stderr,"[*] Resume aus %s (%zu Eintraege). Regel-Signatur: %s\n",g_stateFile.c_str(),g_state.size(),rhash.c_str());
    fprintf(stderr,"[*] %zu Modi x Laenge %d-%d = %ld Bloecke (lange Laengen >=%d zusaetzlich nach 2. Wort aufgeteilt).\n",
            modeIdx.size(),gMinLen,gMaxLen,totalShards,SHARD2_MINLEN);
    fprintf(stderr,"    Bereits Erledigtes wird uebersprungen. (Abbruch mit Strg+C jederzeit moeglich; fertige Bloecke bleiben gespeichert.)\n");

    for(int mi:modeIdx){
        const Mode&m=MODES[mi];
        for(int len=gMinLen; len<=gMaxLen; len++){
            bool sh=sharded(len);
            for(int fwIdx:fwList){
                // Bei langen Laengen je zweitem Wort ein eigener Block, sonst ein Block (2. Wort frei).
                const vector<int>& sws = swOf[fwIdx];
                int nInner = sh ? (int)sws.size() : 1;
                for(int si=0; si<nInner; si++){
                    int swIdx = sh ? sws[si] : -1;
                    shard++;
                    // Block-Schluessel 'fw': bei 2-Wort-Aufteilung "wort1>wort2", sonst "wort1".
                    string fw = (swIdx>=0) ? (W[fwIdx].text+">"+W[swIdx].text) : W[fwIdx].text;
                    // Alle fuer DIESEN Block (Mode/Laenge/Startwort) bereits erledigten
                    // Konfigurationen sammeln. Jede liefert einen CompiledCover; ein Kandidat,
                    // den irgendeine davon schon erzeugt hat, wird nicht erneut getestet.
                    // So kostet weder eine Wort- NOCH eine Regelaenderung je Fortschritt.
                    bool full=false;
                    vector<CompiledCover> covers;
                    uint64_t coverSig=0;
                    for(const DoneRec&d:g_state){
                        if(d.mode!=m.code||d.len!=len||d.fw!=fw) continue;
                        unordered_set<string> rs=csvToSet(d.wcsv);
                        bool sameRules = !d.rules.empty() ? (d.rules==curRules) : (d.rhash==rhash);
                        // Komplett erledigt: identische Regeln UND Alt-Wortset deckt aktuelle Woerter ab.
                        if(sameRules && subsetOf(curSet,rs)){ full=true; break; }
                        // Sonst als Cover-Set aufnehmen:
                        //  - mit gespeicherten Regeln -> exakt auswertbar (jede Aenderung korrekt);
                        //  - Alt-Eintrag ohne Regeltext -> nur nutzbar, wenn Regel-Signatur == aktuell
                        //    (dann sind es die aktuellen Regeln), sonst unbekannt -> ignorieren.
                        if(!d.rules.empty()){
                            CompiledCover c; c.build(rulesParse(d.rules), rs); covers.push_back(std::move(c));
                        } else if(d.rhash==rhash){
                            CompiledCover c; c.build(rulesParse(curRules), rs); covers.push_back(std::move(c));
                        } else continue;
                        coverSig = coverSig*1099511628211ull ^ (uint64_t)std::hash<string>{}(d.rhash+"#"+d.wcsv+"#"+d.rules);
                    }
                    if(full){ skipped++; continue; }

                    // Haeppchengroesse so, dass ein hashcat-Lauf ~gCheckpointSec dauert (aus H/s).
                    uint64_t chunk=UINT64_MAX;
                    if(gCheckpointSec>0){
                        double hs=hsForHash(m.hash,m.code);
                        if(hs>0){ chunk=(uint64_t)(hs*gCheckpointSec); if(chunk<100000) chunk=100000; }
                    }
                    // Zwischenstand fortsetzen?
                    uint64_t offset=0;
                    { auto it=g_part.find(partKey(m.code,len,fw,rhash,curCsv,coverSig)); if(it!=g_part.end()) offset=it->second; }

                    char startDesc[96];
                    if(swIdx>=0) snprintf(startDesc,sizeof(startDesc),"%s > %s",W[fwIdx].text.c_str(),W[swIdx].text.c_str());
                    else         snprintf(startDesc,sizeof(startDesc),"%s",W[fwIdx].text.c_str());

                    fprintf(stderr,"\n[Block %ld/%ld] %s XTS-%s (%s) -m %s | Laenge %d | Start '%s'%s\n",
                            shard,totalShards,m.hash,m.size,m.cipher,m.code,len,startDesc,
                            covers.empty()?"":"  [nur NEUE Kombinationen]");
                    if(chunk!=UINT64_MAX) fprintf(stderr,"    Checkpoint alle ~%ds (%s Kandidaten/Haeppchen)\n",
                            gCheckpointSec,fmtCount((double)chunk).c_str());
                    if(offset>0) fprintf(stderr,"    Fortsetzung ab Kandidat %llu\n",(unsigned long long)offset);

                    // Trocken-Probe: Erzeugt dieser Block (ab offset, nach Cover-Filter) ueberhaupt
                    // noch etwas Neues? Wenn nicht -> hashcat gar nicht erst starten (spart den
                    // Start und verhindert, dass hashcat bei LEERER Eingabe einen Fehlercode liefert,
                    // der faelschlich als GPU-Problem gewertet wuerde). Block trotzdem als erledigt
                    // markieren, damit ein identischer Folgelauf ihn sofort ueberspringt.
                    { Worker probe; probe.init(false,len,len); probe.out=nullptr;
                      for(const CompiledCover&c:covers) probe.coverSets.push_back(&c);
                      probe.skipN=offset; probe.limitN=1; probe.firstWords.push_back(fwIdx); probe.secondWord=swIdx;
                      probe.gen(0);
                      if(probe.outCount==0){
                          fprintf(stderr,"    -> nichts Neues zu testen, Block uebersprungen.\n");
                          appendDone(m.code,len,fw,rhash,curCsv,curRules); skipped++; continue;
                      }
                    }
                    if(!covers.empty()) deltas++; else tested++;

                    char cmd[768];
                    snprintf(cmd,sizeof(cmd),".\\hashcat.exe -a 0 -m %s -D %d -w %d \"%s\" -o geknackt.txt --potfile-disable --status --status-timer=5 --hwmon-temp-abort=%d",
                             m.code,devNum(),gWorkload,targetFile.c_str(),tempAbort);

                    bool blockDone=false;
                    while(!blockDone){
                        remove("geknackt.txt");
                        FILE*hc=POPEN(cmd,"wb"); if(!hc){ fprintf(stderr,"[-] hashcat-Start fehlgeschlagen.\n"); return 1; }
                        Worker wk; wk.init(false,len,len); wk.out=hc;
                        for(const CompiledCover&c:covers) wk.coverSets.push_back(&c);
                        wk.skipN=offset; wk.limitN=chunk; wk.firstWords.push_back(fwIdx); wk.secondWord=swIdx;
                        g_progress=0; g_branchDone=0; g_branchTotal=1; g_monitorRun=true;
                        thread mon(monitor,"gen",false,5000);
                        wk.gen(0); wk.flushOut(); fflush(hc);
                        g_monitorRun=false; mon.join();
                        int ec=exitCodeOf(PCLOSE(hc));
                        FILE*res=fopen("geknackt.txt","rb");
                        if(res){ fclose(res);
                            fprintf(stderr,"\n[+] ERFOLG! %s + XTS-%s (%s), -m %s | Start '%s'\nPasswort (geknackt.txt):\n",
                                    m.hash,m.size,m.cipher,m.code,startDesc);
                            ifstream f("geknackt.txt"); string s; while(getline(f,s)) fprintf(stderr,"%s\n",s.c_str());
                            return 0;
                        }
                        // hashcat: 0=geknackt, 1=erschoepft(ok). Alles andere = Fehler (z.B. keine GPU).
                        if(ec!=0 && ec!=1){
                            fprintf(stderr,"\n[-] hashcat lief nicht korrekt (Exit %d) - vermutlich keine GPU / kein Rechendevice.\n",ec);
                            fprintf(stderr,"    Block wird NICHT als erledigt markiert. Abbruch, damit nichts faelschlich uebersprungen wird.\n");
                            return 2;
                        }
                        uint64_t emitted=wk.outCount; offset+=emitted;
                        if(chunk==UINT64_MAX || emitted<chunk) blockDone=true;    // Block erschoepft
                        else writePart(m.code,len,fw,rhash,curCsv,coverSig,offset); // Zwischenstand sichern (~alle gCheckpointSec)
                    }
                    appendDone(m.code,len,fw,rhash,curCsv,curRules);   // Block + seine Regeln als erledigt markieren
                }
            }
        }
    }
    fprintf(stderr,"\n[=] Durchlauf fertig. Neu getestet: %d Bloecke, Delta-Laeufe: %d, uebersprungen (schon erledigt): %d.\n",tested,deltas,skipped);
    fprintf(stderr,"    Kein Treffer. Aenderst du Woerter/Laenge, merkt sich vcrack diesen Stand und testet nur Neues.\n");
    return 0;
}

// ------------------------------- DOS-Dashboard -----------------------
static string rep(const char* u,int n){ string s; for(int i=0;i<n;i++) s+=u; return s; }
static int cols(const string&s){ int c=0; for(unsigned char b:s) if((b&0xC0)!=0x80) c++; return c; }
static string padTo(const string&s,int w){ string r=s; for(int i=cols(s);i<w;i++) r+=' '; return r; }
static string barOf(double frac,int w){ if(frac<0)frac=0; if(frac>1)frac=1; int n=(int)(frac*w+0.5); return rep("█",n)+rep(" ",w-n); }
static const int DASH_W=64;
static void dashBox(const vector<string>&lines, bool firstTime){
    if(firstTime) fprintf(stderr,"\033[2J");
    fprintf(stderr,"\033[H");
    fprintf(stderr,"╔%s╗\n", rep("═",DASH_W+2).c_str());
    for(const string&ln:lines){
        if(ln=="\x01") fprintf(stderr,"╠%s╣\n", rep("═",DASH_W+2).c_str());
        else fprintf(stderr,"║ %s ║\n", padTo(ln,DASH_W).c_str());
    }
    fprintf(stderr,"╚%s╝\n", rep("═",DASH_W+2).c_str());
    fflush(stderr);
}

// Monte-Carlo-Schaetzer (Knuth). Fuellt perLen[7..10] (Mittelwerte) und ciPct (95%).
static double sampleKeyspace(int mn,int mx,int samples,double perLen[11],double& ciPct,bool dash){
    std::mt19937_64 rng(0xC0FFEEULL ^ (uint64_t)Nw);
    vector<int> path(Nw,0); vector<char> used(Nw,0);
    vector<int> valid; valid.reserve(Nw);
    double sum=0,sumsq=0; double plSum[11]={0};
    auto t0=chrono::steady_clock::now(); bool firstDraw=true; string rh=rulesSignature();
    for(int s=0;s<samples;s++){
        for(int k=0;k<Nw;k++) used[k]=0; uint64_t usedMask=0;
        int everIso=0,everPart=0,everForever=0,everForEver=0,ever4Ever=0;
        double w=1.0, est=0.0; int depth=0;
        for(;;){
            if(depth>=mn && depth<=mx){
                bool ok=true;
                for(const TailRule&tr:tailRules){ for(int k=depth-tr.n;k<depth;k++){ if(k<0) continue; if((tr.mask>>path[k])&1){ ok=false; break; } } if(!ok) break; }
                if(ok){ est+=w; if(depth<=10) plSum[depth]+=w; }
            }
            if(depth>=mx) break;
            valid.clear();
            for(int i=0;i<Nw;i++){
                if(used[i]) continue;
                if(depth+1<wordMinPos[i]) continue;
                if(maxOneEver){
                    int tiso=everIso+(W[i].isEver?1:0);
                    int tforever=everForever+(W[i].isForever?1:0);
                    int tpart=everPart,tForEver=everForEver,t4Ever=ever4Ever;
                    if(depth>0){ const Word&last=W[path[depth-1]]; if((last.isFor||last.is4)&&W[i].isEver){ tpart++; if(last.isFor) tForEver++; else t4Ever++; } }
                    int ec=(tforever>0)+(tForEver>0)+(t4Ever>0)+((tiso-tpart)>0);
                    if(ec>1) continue;
                }
                if(depth>0){ int last=path[depth-1]; if((forbiddenNext[last]>>i)&1) continue; }
                bool excl=false; for(uint64_t g:exclusiveGroups){ if(((g>>i)&1)&&(usedMask&g)){ excl=true; break; } }
                if(excl) continue;
                valid.push_back(i);
            }
            int c=(int)valid.size();
            if(c==0) break;
            w*=(double)c;
            int pick=valid[(size_t)(rng()%(uint64_t)c)];
            if(W[pick].isEver) everIso++;
            if(W[pick].isForever) everForever++;
            if(depth>0){ const Word&last=W[path[depth-1]]; if((last.isFor||last.is4)&&W[pick].isEver){ everPart++; if(last.isFor) everForEver++; else ever4Ever++; } }
            path[depth]=pick; used[pick]=1; usedMask|=(1ull<<pick); depth++;
        }
        sum+=est; sumsq+=est*est;
        if(((s & 0x3FFF)==0) || s==samples-1){
            int done=s+1;
            double el=chrono::duration<double>(chrono::steady_clock::now()-t0).count();
            double mean=sum/done; double var=sumsq/done-mean*mean; if(var<0)var=0;
            double ci=1.96*sqrt(var/done); double pc=mean>0?100.0*ci/mean:0;
            double rate=el>0?done/el/1e3:0;
            if(dash && g_tty){
                vector<string> L; char b[192];
                snprintf(b,sizeof(b),"VCRACK2 - KEYSPACE-HOCHRECHNUNG            [ %5.1f%% ]",100.0*done/samples); L.push_back(b);
                L.push_back("\x01");
                snprintf(b,sizeof(b),"Woerter: %-3d   Laenge: %d-%d   Regeln#: %s",Nw,mn,mx,rh.c_str()); L.push_back(b);
                snprintf(b,sizeof(b),"Pfade:   %-9d   Tempo: %6.0fk/s   Zeit: %4.1fs",done,rate,el); L.push_back(b);
                L.push_back("\x01");
                L.push_back("Laenge   Kandidaten            Verteilung");
                for(int d=mn; d<=mx; d++){
                    double pm=plSum[d]/done; double frac=mean>0?pm/mean:0;
                    snprintf(b,sizeof(b)," %2d   %11s   %s %4.1f%%",d,fmtCount(pm).c_str(),barOf(frac,20).c_str(),100*frac);
                    L.push_back(b);
                }
                L.push_back("\x01");
                snprintf(b,sizeof(b),"GESAMT:  %s   +/- %.1f%% (95%%)",fmtCount(mean).c_str(),pc); L.push_back(b);
                dashBox(L,firstDraw); firstDraw=false;
            } else if(dash){
                fprintf(stderr,"\r  ... %d/%d Pfade  ~%s  +/-%.1f%%   ",done,samples,fmtCount(mean).c_str(),pc); fflush(stderr);
            }
        }
    }
    double mean=sum/samples; double var=sumsq/samples-mean*mean; if(var<0)var=0;
    ciPct = mean>0 ? 100.0*1.96*sqrt(var/samples)/mean : 0;
    for(int d=0;d<11;d++) perLen[d]=plSum[d]/samples;
    if(dash && !g_tty) fprintf(stderr,"\n");
    return mean;
}

static int doSample(int samples){
    string h=searchHash(gMinLen,gMaxLen); double cached;
    if(!gJson && cacheGet(h,"exact",cached)){
        fprintf(stderr,"[=] Exakt aus Cache (count): %s (Such-Hash %s)\n",fmtCount(cached).c_str(),h.c_str());
        printf("%.0f\n",cached); return 0;
    }
    double perLen[11]; double ciPct;
    double mean=sampleKeyspace(gMinLen,gMaxLen,samples,perLen,ciPct,!gJson);
    cachePut(h,"est",mean);
    if(gJson){
        printf("{\"total\":%.0f,\"ci_pct\":%.3f,\"min\":%d,\"max\":%d,\"hash\":\"%s\",\"perlen\":{",
               mean,ciPct,gMinLen,gMaxLen,h.c_str());
        bool first=true;
        for(int d=gMinLen; d<=gMaxLen; d++){ printf("%s\"%d\":%.0f",first?"":",",d,perLen[d]); first=false; }
        printf("}}\n");
    } else {
        fprintf(stderr,"\n[=] Schaetzung gespeichert (Such-Hash %s). Exakt per 'vcrack count'.\n",h.c_str());
        printf("%.0f\n",mean);
    }
    return 0;
}

// ------------------------------- Header-Extraktion ------------------
static string jsonEsc(const string&s){
    string o; for(char c:s){ if(c=='\\'||c=='"'){ o+='\\'; o+=c; } else if(c=='\n'){ o+="\\n"; } else o+=c; } return o;
}
// Kopiert die ersten gHeaderBytes Bytes des Quell-Volumes nach targetFile.
// VeraCrypt prueft nur die ersten 512 Byte (Standard) bzw. ab Offset 65536 (versteckt);
// 512 KB decken beide ab. Fuer Datei-Container reicht ein einfaches fopen/fread.
static int doHeader(){
    if(gSrc.empty()){
        fprintf(stderr,"[-] Keine Quelle. Nutze: vcrack header --src <volume|containerdatei> [--bytes N] [--target F]\n");
        return 2;
    }
    FILE* in=fopen(gSrc.c_str(),"rb");
    if(!in){ fprintf(stderr,"[-] Quelle nicht lesbar: %s\n",gSrc.c_str());
        if(gJson) printf("{\"error\":\"quelle nicht lesbar\",\"src\":\"%s\"}\n",jsonEsc(gSrc).c_str());
        return 2; }
    FILE* out=fopen(targetFile.c_str(),"wb");
    if(!out){ fprintf(stderr,"[-] Zieldatei nicht schreibbar: %s\n",targetFile.c_str()); fclose(in);
        if(gJson) printf("{\"error\":\"ziel nicht schreibbar\",\"target\":\"%s\"}\n",jsonEsc(targetFile).c_str());
        return 2; }
    vector<unsigned char> buf((size_t)(gHeaderBytes>0?gHeaderBytes:512));
    size_t got=fread(buf.data(),1,buf.size(),in); fclose(in);
    size_t wr=got?fwrite(buf.data(),1,got,out):0; fclose(out);
    if(got==0 || wr!=got){ fprintf(stderr,"[-] Lesen/Schreiben fehlgeschlagen (gelesen %zu, geschrieben %zu).\n",got,wr);
        if(gJson) printf("{\"error\":\"io\",\"read\":%zu,\"written\":%zu}\n",got,wr);
        return 1; }
    if(gJson){
        printf("{\"ok\":true,\"src\":\"%s\",\"target\":\"%s\",\"bytes\":%zu}\n",
               jsonEsc(gSrc).c_str(),jsonEsc(targetFile).c_str(),got);
    } else {
        fprintf(stderr,"[+] %zu Bytes aus '%s' extrahiert -> '%s'.\n",got,gSrc.c_str(),targetFile.c_str());
        if(got<(size_t)gHeaderBytes) fprintf(stderr,"    (Quelle kleiner als angefordert - vollstaendig kopiert.)\n");
        fprintf(stderr,"    Damit kann 'vcrack run'/'estimate' arbeiten. Original bleibt unveraendert.\n");
    }
    return 0;
}

// ------------------------------- main --------------------------------
static void usage(){ fprintf(stderr,
    "vcrack <rules|gen|count|sample|bench|estimate|run|header> [optionen]\n"
    "  --rules F --words F --target F --min N --max N --skip N --limit N\n"
    "  --hash H --size S --device gpu|cpu|both --temp N --samples N --rebench --checkpoint SEK --workload 1..4\n"
    "  header --src <volume> [--bytes N] [--target F] = Header aus VeraCrypt-Volume extrahieren (Standard 512 KB)\n"
    "  estimate = Keyspace + Verteilung je Laenge + H/s je Hash + ETA in einem Schritt (ersetzt sample+bench)\n"
    "  --checkpoint SEK = Zwischenspeicherung des run-Fortschritts spaetestens alle SEK Sekunden (Standard 300, 0=aus)\n"
    "  sample/estimate = schnelle Keyspace-Schaetzung mit +/- Fehlerspanne (DOS-Dashboard)\n"
    "  count = exakt (langsamer); Ergebnis teilen sich count/sample/estimate per Such-Hash-Cache\n"
    "  estimate: H/s je Hash gemessen (ein Benchmark statt zwoelf) + Bench-Cache; --rebench misst neu\n"); }

int main(int argc,char**argv){
    enableVT(); g_tty = ISATTY(FILENO(stderr))!=0;
    if(argc<2){ usage(); return 2; }
    string cmd=argv[1];
    for(int i=2;i<argc;i++){ string a=argv[i];
        auto val=[&](const char*d)->string{ return (i+1<argc)? string(argv[++i]) : string(d); };
        if(a=="--rules") rulesFile=val("rules.txt");
        else if(a=="--words") wordsFile=val("list.txt");
        else if(a=="--target"){ targetFile=val("Fvc_header.tc"); targetSet=true; }
        else if(a=="--vc") vcFile=val("");
        else if(a=="--min") cliMin=atoi(val("7").c_str());
        else if(a=="--max") cliMax=atoi(val("10").c_str());
        else if(a=="--skip") gSkip=strtoull(val("0").c_str(),nullptr,10);
        else if(a=="--limit") gLimit=strtoull(val("0").c_str(),nullptr,10);
        else if(a=="--hash") fHash=val("");
        else if(a=="--size") fSize=val("");
        else if(a=="--device") device=val("gpu");
        else if(a=="--temp") tempAbort=atoi(val("90").c_str());
        else if(a=="--samples") gSamples=atoi(val("300000").c_str());
        else if(a=="--rebench") gRebench=true;
        else if(a=="--checkpoint") gCheckpointSec=atoi(val("300").c_str());
        else if(a=="--workload") gWorkload=atoi(val("3").c_str());
        else if(a=="--src") gSrc=val("");
        else if(a=="--bytes") gHeaderBytes=atol(val("524288").c_str());
        else if(a=="--json") gJson=true;
        else fprintf(stderr,"[!] Unbekannte Option: %s\n",a.c_str());
    }
    if(cmd=="header") return doHeader();   // braucht weder Wortliste noch Regeln

    bool haveRules;
    if(!vcFile.empty()){
        // .vc ist die einzige Quelle: Woerter + Regeln + Header + (spaeter) Fortschritt.
        if(!loadVcFile(vcFile)){ fprintf(stderr,"[-] .vc nicht lesbar oder ohne Woerter: %s\n",vcFile.c_str()); return 2; }
        g_stateFile=vcFile;                 // DONE/PART werden an die .vc angehaengt ([progress] ist letzte Sektion)
        haveRules = !RULES.empty();
    } else {
        if(!loadWords(wordsFile)){ fprintf(stderr,"[-] Wortliste nicht lesbar: %s\n",wordsFile.c_str()); return 2; }
        haveRules = loadRulesFile(rulesFile);
    }
    Nw=(int)W.size(); if(Nw==0){ fprintf(stderr,"[-] Wortliste leer.\n"); return 2; }
    if(Nw>64){ fprintf(stderr,"[-] Mehr als 64 Woerter nicht unterstuetzt (Masken).\n"); return 2; }

    bool fromFile=haveRules;
    if(!fromFile) installDefaultRules();
    compileRules();

    gMinLen=(cliMin>=0?cliMin:ruleMin); gMaxLen=(cliMax>=0?cliMax:ruleMax);
    if(gMinLen<1) gMinLen=1; if(gMaxLen>Nw) gMaxLen=Nw;
    if(gMinLen>gMaxLen){ fprintf(stderr,"[-] min > max\n"); return 2; }

    if(cmd=="rules"){ printRules(fromFile); return 0; }
    if(cmd=="gen")      return doGen();
    if(cmd=="count")    return doCount();
    if(cmd=="sample")   return doSample(gSamples);
    if(cmd=="bench")    return doBench();
    if(cmd=="estimate") return doEstimate();
    if(cmd=="run")      return doRun();
    if(cmd=="header")   return doHeader();   // (wird oben bereits abgefangen)
    usage(); return 2;
}
