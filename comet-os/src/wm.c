/* Comet OS window manager / compositor / desktop shell */
#include "kernel.h"

#define MAXW 24
#define TB_H 52
#define TH 34
#define ANIM_T 18

theme_t T; int g_dark=1,g_anim=1,g_night=0,g_bright=100,g_vol=60,g_air=0,g_focus=0;
u32 g_accent=0x4C8DFF; int g_arg;
const u32 accents[8]={0x4C8DFF,0x8B5CF6,0xF472B6,0xF87171,0xFB923C,0xFACC15,0x34D399,0x22D3EE};
const char*accent_names[8]={"Comet blue","Violet","Pink","Coral","Amber","Gold","Mint","Cyan"};
win_t wins[MAXW]; static int zord[MAXW],nz,focus_i=-1,cur_desk,ndesk=1,next_pid=100,dirty=1,cursor_dirty,cascade;
int cpu_pct,cpu_hist[60],comp_pct; static u64 idle_cyc,comp_cyc,last_tsc;
static int tm_h,tm_m,tm_s,tm_d,tm_mo,tm_y; static win_t*hover_w;
void wm_redraw(void){dirty=1;}

/* ------------------------------------------------ theme ---------------- */
void theme_apply(void){
    T.accent=g_accent;T.accent2=blend(g_accent,0xFFFFFF,70);T.good=0x34D399;T.bad=0xF0555B;
    if(g_dark){T.text=0xF5F6FA;T.dim=0x9AA0B4;T.win=0x1C1F2B;T.win2=0x2A3044;T.tint=0x121626;T.hover=0x394066;T.border=0x3A4060;T.field=0x141722;}
    else{T.text=0x14171F;T.dim=0x5B6275;T.win=0xFAFBFE;T.win2=0xE9EDF8;T.tint=0xEEF2FB;T.hover=0xD3DBF2;T.border=0xC8CFE3;T.field=0xFFFFFF;}
    wall_generate(g_dark,g_accent);dirty=1;
}
static int lum(u32 c){return (((c>>16)&255)*299+((c>>8)&255)*587+(c&255)*114)/1000;}
static int ease(int t,int dur){if(t>=dur)return 1000;if(t<0)return 0;int u=1000-t*1000/dur;return 1000-(u*u/1000)*u/1000;}
static int lerp(int a,int b,int p){return a+(b-a)*p/1000;}
static int inr(int px,int py,int x,int y,int w,int h){return px>=x&&px<x+w&&py>=y&&py<y+h;}

/* ------------------------------------------------ toasts --------------- */
static struct{char msg[80];u32 t0;}toasts[3];
void toast(const char*m){if(g_focus)return;for(int i=2;i>0;i--)toasts[i]=toasts[i-1];scopy(toasts[0].msg,m,80);toasts[0].t0=ticks;dirty=1;}

/* ------------------------------------------------ window helpers ------- */
static int widx(win_t*w){return (int)(w-wins);}
static int is_anim(win_t*w){return w->anim_kind>=1&&w->anim_kind<=4;}
static int vis(win_t*w){return w->used&&w->desktop==cur_desk&&!w->minimized&&!is_anim(w)&&!w->closing;}
static void z_remove(int i){for(int k=0;k<nz;k++)if(zord[k]==i){for(int j=k;j<nz-1;j++)zord[j]=zord[j+1];nz--;return;}}
static void z_top(int i){z_remove(i);zord[nz++]=i;}
static void anim_start(win_t*w,int kind){w->anim_kind=kind;w->anim_t0=ticks;dirty=1;}
static void wa_rect(int*x,int*y,int*w,int*h){*x=0;*y=0;*w=SW;*h=SH-TB_H;}
static void switch_desktop(int d);
static void focus_next(void){
    focus_i=-1;for(int k=nz-1;k>=0;k--){win_t*w=&wins[zord[k]];if(w->used&&w->desktop==cur_desk&&!w->minimized&&!w->closing){focus_i=zord[k];return;}}
}
void win_focus(win_t*w){
    if(!w||!w->used)return;
    if(w->desktop!=cur_desk)switch_desktop(w->desktop);
    if(w->minimized){w->minimized=0;anim_start(w,4);}
    z_top(widx(w));focus_i=widx(w);dirty=1;
}
win_t*win_open_arg(int app,int arg){
    if(!apps[app].installed){toast("Install it from the Store first");return 0;}
    if(app==APP_SET||app==APP_STORE||app==APP_MON||app==APP_TASK||app==APP_SETUP||app==APP_GAME||app==APP_VIEW||app==APP_METEORITE){
        for(int i=0;i<MAXW;i++)if(wins[i].used&&wins[i].app==app&&!wins[i].closing){if(arg)g_arg=arg;win_focus(&wins[i]);if(app==APP_VIEW&&arg){g_arg=arg;}return &wins[i];}
    }
    int s=-1;for(int i=0;i<MAXW;i++)if(!wins[i].used){s=i;break;}
    if(s<0){toast("Too many windows open");return 0;}
    win_t*w=&wins[s];memset(w,0,sizeof *w);w->used=1;w->app=app;w->pid=next_pid++;w->desktop=cur_desk;w->born=ticks;
    w->w=MIN(apps[app].dw,SW-20);w->h=MIN(apps[app].dh,SH-TB_H-20);
    w->x=(SW-w->w)/2+(cascade%6)*28-70;w->y=(SH-TB_H-w->h)/2+(cascade%6)*24-60;cascade++;
    w->x=CLAMP(w->x,8,MAX(8,SW-w->w-8));w->y=CLAMP(w->y,8,MAX(8,SH-TB_H-w->h-8));
    scopy(w->title,apps[app].name,48);g_arg=arg;
    if(apps[app].init)apps[app].init(w);
    z_top(s);focus_i=s;anim_start(w,1);return w;
}
win_t*win_open(int app){return win_open_arg(app,0);}
void win_close(win_t*w){
    if(!w||!w->used||w->closing)return;
    if(apps[w->app].close)apps[w->app].close(w);
    w->closing=1;anim_start(w,2);if(widx(w)==focus_i)focus_next();
    if(!g_anim)w->anim_t0-=100;
}
static void win_free(win_t*w){int i=widx(w);z_remove(i);if(w->d)kfree(w->d);w->d=0;w->used=0;if(focus_i==i)focus_next();}
static void zone_rect(int z,int*x,int*y,int*w,int*h){
    int wx,wy,ww,wh;wa_rect(&wx,&wy,&ww,&wh);int m=6;int hw=(ww-3*m)/2,hh=(wh-3*m)/2,tw=(ww-3*m)*2/3;
    *x=m;*y=m;*w=ww-2*m;*h=wh-2*m;
    switch(z){
    case 1:*w=hw;break;case 2:*x=2*m+hw;*w=ww-3*m-hw;break;
    case 3:*w=hw;*h=hh;break;case 4:*x=2*m+hw;*w=ww-3*m-hw;*h=hh;break;
    case 5:*w=hw;*y=2*m+hh;*h=wh-3*m-hh;break;case 6:*x=2*m+hw;*w=ww-3*m-hw;*y=2*m+hh;*h=wh-3*m-hh;break;
    case 7:*w=tw;break;case 8:*x=2*m+tw;*w=ww-3*m-tw;break;
    case 9:*x=0;*y=0;*w=ww;*h=wh;break;
    }
}
static void win_goto(win_t*w,int x,int y,int ww,int hh){
    w->fx=w->x;w->fy=w->y;w->fw=w->w;w->fh=w->h;w->tx=x;w->ty=y;w->tw=ww;w->th=hh;
    if(!g_anim){w->x=x;w->y=y;w->w=ww;w->h=hh;return;}anim_start(w,5);
}
static void win_snap(win_t*w,int z){
    if(!w->max){w->rx=w->x;w->ry=w->y;w->rw=w->w;w->rh=w->h;}
    int x,y,ww,hh;zone_rect(z,&x,&y,&ww,&hh);w->max=(z==9)?1:2;
    ww=MAX(ww,apps[w->app].minw);hh=MAX(hh,apps[w->app].minh);win_goto(w,x,y,ww,hh);
}
static void win_restore(win_t*w){if(w->max){w->max=0;win_goto(w,w->rx,w->ry,w->rw,w->rh);}}
static void win_minimize(win_t*w){if(w->minimized)return;w->minimized=1;anim_start(w,3);if(widx(w)==focus_i)focus_next();}
static void win_togglemax(win_t*w){if(w->max==1)win_restore(w);else win_snap(w,9);}
void ui_title(win_t*w,const char*t){scopy(w->title,t,48);}

static void anims_update(void){
    for(int i=0;i<MAXW;i++){win_t*w=&wins[i];if(!w->used||!w->anim_kind)continue;
        int t=ticks-w->anim_t0;if(!g_anim)t=ANIM_T;
        if(t>=ANIM_T){
            if(w->anim_kind==5){w->x=w->tx;w->y=w->ty;w->w=w->tw;w->h=w->th;}
            int k=w->anim_kind;w->anim_kind=0;if(k==2){win_free(w);}
        }else if(w->anim_kind==5){int p=ease(t,ANIM_T);w->x=lerp(w->fx,w->tx,p);w->y=lerp(w->fy,w->ty,p);w->w=lerp(w->fw,w->tw,p);w->h=lerp(w->fh,w->th,p);}
        dirty=1;
    }
}

/* ------------------------------------------------ ui helpers ----------- */
void ui_text(win_t*w,int x,int y,const char*s,u32 c,int sc){draw_text(w->cx+x,w->cy+y,s,c,sc);}
void ui_fill(win_t*w,int x,int y,int bw,int bh,u32 c,int r){rrect(w->cx+x,w->cy+y,bw,bh,r,c,255);}
void ui_bg(win_t*w,u32 c){rrect_ex(w->cx,w->cy,w->cw,w->ch,w->max==1?0:10,c,255,w->max==1?0:12,0);}
int w_hover(win_t*w,int x,int y,int bw,int bh){return hover_w==w&&inr(mouse_x,mouse_y,w->cx+x,w->cy+y,bw,bh);}
void w_hit(win_t*w,int x,int y,int bw,int bh,int id){if(w->nhits<100){hit_t*h=&w->hits[w->nhits++];h->x=x;h->y=y;h->w=bw;h->h=bh;h->id=id;}}
void ui_button(win_t*w,int id,int x,int y,int bw,int bh,const char*label,int style){
    int hv=w_hover(w,x,y,bw,bh);u32 bg,fg=T.text;
    if(style==1){bg=hv?T.accent2:T.accent;fg=lum(T.accent)>150?0x0B1020:0xFFFFFF;}
    else if(style==2){bg=hv?0xFF6B70:T.bad;fg=0xFFFFFF;}
    else bg=hv?T.hover:T.win2;
    ui_fill(w,x,y,bw,bh,bg,6);
    draw_text(w->cx+x+(bw-text_w(label,1))/2,w->cy+y+(bh-8)/2,label,fg,1);
    if(id>=0)w_hit(w,x,y,bw,bh,id);
}
void ui_bar(win_t*w,int x,int y,int bw,int bh,int pct,u32 col){
    pct=CLAMP(pct,0,100);ui_fill(w,x,y,bw,bh,T.field,bh/2);if(pct)ui_fill(w,x,y,MAX(bh,bw*pct/100),bh,col,bh/2);
}

/* ------------------------------------------------ drawing windows ------ */
typedef struct{int kind,app,x,y,w;}tbi;static tbi tbs[24];static int ntb;
static const int pins[7]={APP_METEORITE,APP_FILES,APP_TERM,APP_NOTES,APP_VIEW,APP_STORE,APP_SET};
static void tb_layout(void){
    ntb=0;tbs[ntb++]=(tbi){0,0,0,0,44};tbs[ntb++]=(tbi){1,0,0,0,44};tbs[ntb++]=(tbi){2,0,0,0,44};
    for(int i=0;i<7;i++)tbs[ntb++]=(tbi){3,pins[i],0,0,44};
    for(int i=0;i<MAXW;i++)if(wins[i].used&&!wins[i].closing){int a=wins[i].app,f=0;for(int k=3;k<ntb;k++)if(tbs[k].app==a)f=1;if(!f&&ntb<24)tbs[ntb++]=(tbi){3,a,0,0,44};}
    int tot=ntb*48-4,x0=(SW-tot)/2;for(int i=0;i<ntb;i++){tbs[i].x=x0+i*48;tbs[i].y=SH-TB_H+4;}
}
static void tb_center(int app,int*cx,int*cy){*cx=SW/2;*cy=SH-TB_H/2;for(int i=0;i<ntb;i++)if(tbs[i].kind==3&&tbs[i].app==app){*cx=tbs[i].x+22;*cy=tbs[i].y+22;}}

static void ghost(win_t*w){
    int t=ticks-w->anim_t0,p=ease(g_anim?t:ANIM_T,ANIM_T),k=w->anim_kind;int gx,gy,gw,gh,a;
    if(k==1||k==2){int q=k==1?p:1000-p;int s=900+100*q/1000;gw=w->w*s/1000;gh=w->h*s/1000;gx=w->x+(w->w-gw)/2;gy=w->y+(w->h-gh)/2+(1000-q)*14/1000;a=q*255/1000;}
    else{int q=k==3?p:1000-p,cx,cy;tb_center(w->app,&cx,&cy);gx=lerp(w->x,cx-24,q);gy=lerp(w->y,cy-24,q);gw=lerp(w->w,48,q);gh=lerp(w->h,48,q);a=255-q*190/1000;}
    if(gw<8||gh<8)return;
    shadow(gx,gy,gw,gh,10,8);rmica(gx,gy,gw,gh,10,T.tint,a*200/255);rframe(gx,gy,gw,gh,10,T.accent,a/2);
    int is=CLAMP(MIN(gw,gh)/3,20,72);if(a>90)draw_icon(w->app,gx+(gw-is)/2,gy+(gh-is)/2,is);
}
static void draw_window(win_t*w,int xoff,int foc){
    int r=w->max==1?0:10,x=w->x+xoff,y=w->y,ww=w->w,hh=w->h;
    if(w->max!=1)shadow(x,y,ww,hh,r,foc?16:8);
    rrect_ex(x,y,ww,hh,r,T.win,255,w->max==1?0:15,0);
    rrect_ex(x,y,ww,TH,r,T.tint,foc?150:215,w->max==1?0:3,1);
    fill_rect_a(x,y+TH-1,ww,1,T.border,foc?120:70);
    rframe(x,y,ww,hh,r,foc?T.accent:T.border,foc?150:100);
    draw_icon(w->app,x+11,y+9,17);
    draw_text(x+36,y+(TH-8)/2,w->title,foc?T.text:T.dim,1);
    for(int i=0;i<3;i++){
        int bx=x+ww-46*(3-i),hv=hover_w==w&&xoff==0&&inr(mouse_x,mouse_y,bx,y,46,TH);
        if(hv)rrect_ex(bx,y+(i==2&&w->max!=1?1:0),46,TH-1,r>0&&i==2?r:0,i==2?0xE5484D:T.hover,i==2?255:200,i==2&&w->max!=1?2:0,0);
        icon_color(i==2&&hv?0xFFFFFF:(foc?T.text:T.dim),0);draw_icon(i==0?IC_MIN:(i==1?IC_MAX:IC_CLOSE),bx+16,y+10,14);
    }
    w->cx=x;w->cy=y+TH;w->cw=ww;w->ch=hh-TH;w->nhits=0;
    cliprect c=clip_push(w->cx,w->cy,w->cw,w->ch);
    u64 t0=rdtsc();if(apps[w->app].draw)apps[w->app].draw(w);w->cycles+=rdtsc()-t0;
    clip_pop(c);
}
static void draw_wins(int d,int xoff){
    for(int k=0;k<nz;k++){win_t*w=&wins[zord[k]];if(!w->used||w->desktop!=d)continue;
        if(is_anim(w)){if(xoff==0)ghost(w);continue;}
        if(w->minimized)continue;
        draw_window(w,xoff,zord[k]==focus_i&&xoff==0);}
}

/* ------------------------------------------------ shell state ---------- */
enum{FL_NONE,FL_START,FL_CAL,FL_QS,FL_WIDGETS};
static int fly,fly_t0,pwr_open,cal_m,cal_y,taskview,alttab,alt_sel,naltl,altlist[MAXW];
static char sq[40];static int sqlen,ssel,sres[10],nsres;
static int ctx_open,ctx_x,ctx_y,snap_cand=-1,snap_t0,snap_hz=-1,snap_prev,dsel=-1;
static int dsk_from=-1,dsk_to,dsk_t0,recent[6],nrecent;
static const char*ctx_items[6]={"Open Terminal","New note","Toggle dark mode","Personalize","Display settings","Task Manager"};
static const int dicons[6]={APP_METEORITE,APP_FILES,APP_TERM,APP_VIEW,APP_STORE,APP_SET};
static const char*dow_n[7]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char*mon_n[12]={"January","February","March","April","May","June","July","August","September","October","November","December"};
static int dow(int y,int m,int d){static const int t[12]={0,3,2,5,0,3,5,1,4,6,2,4};if(m<3)y--;return (y+y/4-y/100+y/400+t[m-1]+d)%7;}
static int dim_(int y,int m){static const int d[12]={31,28,31,30,31,30,31,31,30,31,30,31};return m==2&&((y%4==0&&y%100)||y%400==0)?29:d[m-1];}
static void fmt_time(char*b,int sec){int h=tm_h%12;if(!h)h=12;if(sec)ksnprintf(b,24,"%d:%02d:%02d %s",h,tm_m,tm_s,tm_h<12?"AM":"PM");else ksnprintf(b,24,"%d:%02d %s",h,tm_m,tm_h<12?"AM":"PM");}
static void fly_open(int f){if(fly==f){fly=FL_NONE;return;}fly=f;fly_t0=ticks;pwr_open=0;sqlen=0;sq[0]=0;ssel=0;cal_m=tm_mo;cal_y=tm_y;dirty=1;}
static void fly_rect(int*x,int*y,int*w,int*h){
    int ah=SH-TB_H;
    switch(fly){
    case FL_START:*w=MIN(640,SW-20);*h=MIN(700,ah-24);*x=(SW-*w)/2;*y=ah-8-*h;break;
    case FL_CAL:*w=380;*h=MIN(520,ah-20);*x=SW-*w-10;*y=ah-10-*h;break;
    case FL_QS:*w=400;*h=MIN(470,ah-20);*x=SW-*w-10;*y=ah-10-*h;break;
    default:*w=420;*h=ah-24;*x=10;*y=12;
    }
}
static const char*nic_name(void){
    for(int i=0;i<pci_count;i++)if(pci_devs[i].cls==2){u16 v=pci_devs[i].vendor,d=pci_devs[i].device;
        if(v==0x8086)return "Intel PRO/1000";if(v==0x1022&&d==0x2000)return "AMD PCnet-FAST III";if(v==0x10EC)return "Realtek RTL8139";if(v==0x1AF4)return "virtio-net";return "Network controller";}
    return "No adapter";
}
static void recent_add(int n){for(int i=0;i<nrecent;i++)if(recent[i]==n){for(int j=i;j>0;j--)recent[j]=recent[j-1];recent[0]=n;return;}
    if(nrecent<6)nrecent++;for(int j=nrecent-1;j>0;j--)recent[j]=recent[j-1];recent[0]=n;}
void open_node(int n){
    if(n<=0||!fsn[n].used)return;recent_add(n);
    if(fsn[n].dir)win_open_arg(APP_FILES,n);
    else if(ends_with(fsn[n].name,".cim")||ends_with(fsn[n].name,".bmp"))win_open_arg(APP_VIEW,n);
    else if(ends_with(fsn[n].name,".tar"))win_open_arg(APP_FILES,n);
    else win_open_arg(APP_NOTES,n);
}
static void launch(int code){fly=FL_NONE;dirty=1;if(code>=1000)open_node(code-1000);else win_open(code);}
static int pinned_list(int*o){int n=0;for(int i=0;i<APP_COUNT;i++)if(apps[i].installed)o[n++]=i;return n;}
static void start_search(void){
    nsres=0;if(!sqlen)return;
    for(int i=0;i<APP_COUNT&&nsres<10;i++)if(apps[i].installed&&stricontains(apps[i].name,sq))sres[nsres++]=i;
    int f[8];int n=fs_search(sq,f,8);for(int i=0;i<n&&nsres<10;i++)sres[nsres++]=1000+f[i];
}

/* ------------------------------------------------ taskbar -------------- */
static void draw_taskbar(void){
    int y0=SH-TB_H;acrylic(0,y0,SW,TB_H,0,T.tint,g_dark?175:195);fill_rect_a(0,y0,SW,1,0xFFFFFF,36);
    tb_layout();
    for(int i=0;i<ntb;i++){tbi*t=&tbs[i];int hv=inr(mouse_x,mouse_y,t->x,t->y,44,44),act=0,run=0,fo=0;
        if(t->kind==0)act=fly==FL_START;
        if(t->kind==2)act=taskview;
        if(t->kind==3)for(int k=0;k<MAXW;k++)if(wins[k].used&&wins[k].app==t->app&&!wins[k].closing){run++;if(k==focus_i&&!wins[k].minimized)fo=1;}
        if(hv||act||fo)rrect(t->x,t->y,44,44,8,hv?T.hover:T.win2,(act||fo)?200:150);
        int cx=t->x+22,cy=t->y+22;icon_color(T.text,0);
        if(t->kind==0)draw_logo(cx,cy,28,T.accent);else if(t->kind==1)draw_icon(IC_SEARCH,cx-10,cy-10,20);
        else if(t->kind==2)draw_icon(IC_TASKVIEW,cx-11,cy-11,22);else draw_icon(t->app,cx-15,cy-15,30);
        if(run)rrect(cx-(fo?8:3),t->y+39,fo?16:6,3,1,fo?T.accent:T.dim,255);
    }
    for(int i=0;i<ntb;i++){tbi*t=&tbs[i];if(inr(mouse_x,mouse_y,t->x,t->y,44,44)){
        const char*n=t->kind==0?"Start":t->kind==1?"Search":t->kind==2?"Task View":apps[t->app].name;int tw=text_w(n,1)+20;
        acrylic(t->x+22-tw/2,y0-34,tw,26,8,T.tint,215);draw_text(t->x+32-tw/2,y0-25,n,T.text,1);}}
    /* widgets button */
    int hv=inr(mouse_x,mouse_y,8,y0+6,150,40);if(hv||fly==FL_WIDGETS)rrect(8,y0+6,150,40,8,T.hover,170);
    icon_color(T.accent,0);draw_icon(IC_WIDGETS,16,y0+15,22);char b[40];ksnprintf(b,40,"CPU %d%%",cpu_pct);draw_text(46,y0+12,b,T.text,1);
    ksnprintf(b,40,"MEM %d%%",pct(heap_used(),heap_total()));draw_text(46,y0+28,b,T.dim,1);
    /* tray */
    int qx=SW-104-114;hv=inr(mouse_x,mouse_y,qx,y0+6,108,40);if(hv||fly==FL_QS)rrect(qx,y0+6,108,40,8,T.hover,170);
    icon_color(g_air?T.dim:T.text,0);draw_icon(IC_WIFI,qx+14,y0+15,20);icon_color(T.text,0);draw_icon(IC_SPEAKER,qx+44,y0+15,20);draw_icon(IC_BATT,qx+74,y0+15,22);
    int cx=SW-104;hv=inr(mouse_x,mouse_y,cx,y0+6,98,40);if(hv||fly==FL_CAL)rrect(cx,y0+6,98,40,8,T.hover,170);
    char tb[24];fmt_time(tb,0);draw_text(cx+98-8-text_w(tb,1),y0+12,tb,T.text,1);
    ksnprintf(tb,24,"%d/%d/%d",tm_mo,tm_d,tm_y);draw_text(cx+98-8-text_w(tb,1),y0+28,tb,T.dim,1);
    fill_rect_a(SW-5,y0+8,1,TB_H-16,T.dim,120);
}

/* ------------------------------------------------ flyouts -------------- */
static void start_cell(int i,int x,int y,int w,int*cx,int*cy){int cw=(w-48)/6;*cx=x+24+(i%6)*cw;*cy=y+100+(i/6)*84;}
static void draw_start(int x,int y,int w,int h){
    acrylic(x,y,w,h,14,T.tint,g_dark?200:222);
    rrect(x+24,y+20,w-48,40,20,T.field,200);rframe(x+24,y+20,w-48,40,20,T.accent,sqlen?200:70);
    icon_color(T.dim,0);draw_icon(IC_SEARCH,x+38,y+30,20);
    if(sqlen){draw_text(x+68,y+36,sq,T.text,1);if((ticks/50)&1)fill_rect(x+68+text_w(sq,1),y+32,1,16,T.accent);}
    else draw_text(x+68,y+36,"Type here to search apps and files",T.dim,1);
    if(sqlen){
        start_search();draw_text(x+28,y+76,nsres?"Best match":"No results",T.dim,1);
        for(int i=0;i<nsres;i++){int ry=y+96+i*50;int hv=inr(mouse_x,mouse_y,x+20,ry,w-40,46);if(i==ssel||hv)rrect(x+20,ry,w-40,46,8,T.hover,i==ssel?230:170);
            if(sres[i]<1000){draw_icon(sres[i],x+32,ry+7,32);draw_text(x+78,ry+10,apps[sres[i]].name,T.text,1);draw_text(x+78,ry+26,"App",T.dim,1);}
            else{icon_color(T.text,T.win);int n=sres[i]-1000;draw_icon(ends_with(fsn[n].name,".cim")||ends_with(fsn[n].name,".bmp")?IC_IMAGE:IC_FILE,x+38,ry+9,26);char p[64];fs_path(n,p,64);draw_text(x+78,ry+10,fsn[n].name,T.text,1);draw_text(x+78,ry+26,p,T.dim,1);}}
    }else{
        draw_text_b(x+28,y+74,"Pinned",T.text,1);
        int pl[APP_COUNT],n=pinned_list(pl);
        for(int i=0;i<n&&i<18;i++){int cx,cy,cw=(w-48)/6;start_cell(i,x,y,w,&cx,&cy);
            if(inr(mouse_x,mouse_y,cx,cy,cw,80))rrect(cx+2,cy,cw-4,80,8,T.hover,180);
            draw_icon(pl[i],cx+(cw-36)/2,cy+8,36);const char*nm=apps[pl[i]].name;draw_text(cx+(cw-text_w(nm,1))/2,cy+54,nm,T.text,1);}
        int ry=y+100+3*84+4;draw_text_b(x+28,ry,"Recommended",T.text,1);
        if(!nrecent){draw_text(x+28,ry+30,"Files you open will show up here.",T.dim,1);}
        for(int i=0;i<nrecent;i++){int rx=x+24+(i%2)*((w-48)/2),rry=ry+24+(i/2)*48,rw=(w-48)/2;
            if(inr(mouse_x,mouse_y,rx,rry,rw-6,44))rrect(rx,rry,rw-6,44,8,T.hover,180);
            icon_color(T.text,T.win);draw_icon(IC_FILE,rx+10,rry+9,24);draw_text(rx+46,rry+10,fsn[recent[i]].name,T.text,1);draw_text(rx+46,rry+26,fsn[recent[i]].dir?"Folder":"Recently opened",T.dim,1);}
    }
    fill_rect_a(x,y+h-64,w,1,T.dim,60);fill_rect_a(x,y+h-63,w,63,0x000000,g_dark?50:0);
    circle_ex(x+48,y+h-32,16,0,T.accent,255);draw_text(x+43,y+h-36,"G",lum(T.accent)>150?0x0B1020:0xFFFFFF,1);draw_text(x+74,y+h-36,"Guest",T.text,1);
    int hv=inr(mouse_x,mouse_y,x+w-64,y+h-52,40,40);if(hv||pwr_open)rrect(x+w-64,y+h-52,40,40,8,T.hover,200);icon_color(T.text,0);draw_icon(IC_POWER,x+w-54,y+h-42,20);
    if(pwr_open){int px=x+w-170,py=y+h-64-76;acrylic(px,py,150,70,10,T.tint,235);
        for(int i=0;i<2;i++){if(inr(mouse_x,mouse_y,px+6,py+6+i*30,138,28))rrect(px+6,py+6+i*30,138,28,6,T.hover,220);draw_text(px+18,py+16+i*30,i?"Restart":"Shut down",T.text,1);}}
}
static void draw_cal(int x,int y,int w,int h){
    acrylic(x,y,w,h,14,T.tint,g_dark?205:225);char b[40];fmt_time(b,1);draw_text(x+24,y+22,b,T.text,3);
    ksnprintf(b,40,"%s, %s %d",dow_n[dow(tm_y,tm_mo,tm_d)],mon_n[tm_mo-1],tm_d);draw_text(x+24,y+62,b,T.dim,2);
    fill_rect_a(x+16,y+100,w-32,1,T.dim,60);
    ksnprintf(b,40,"%s %d",mon_n[cal_m-1],cal_y);draw_text_b(x+24,y+118,b,T.text,2);
    icon_color(T.text,0);if(inr(mouse_x,mouse_y,x+w-96,y+112,32,28))rrect(x+w-96,y+112,32,28,6,T.hover,200);if(inr(mouse_x,mouse_y,x+w-56,y+112,32,28))rrect(x+w-56,y+112,32,28,6,T.hover,200);
    draw_text(x+w-85,y+122,"<",T.text,1);draw_text(x+w-45,y+122,">",T.text,1);
    int cw=(w-32)/7;for(int i=0;i<7;i++){const char*d[7]={"Su","Mo","Tu","We","Th","Fr","Sa"};draw_text(x+16+i*cw+(cw-16)/2,y+160,d[i],T.dim,1);}
    int first=dow(cal_y,cal_m,1),dm=dim_(cal_y,cal_m),pm=cal_m==1?12:cal_m-1,py=cal_m==1?cal_y-1:cal_y,pd=dim_(py,pm);
    for(int i=0;i<42;i++){int dn,cur=1;int c=i-first+1;if(c<1){dn=pd+c;cur=0;}else if(c>dm){dn=c-dm;cur=0;}else dn=c;
        int cx=x+16+(i%7)*cw,cy=y+184+(i/7)*46;int today=cur&&cal_y==tm_y&&cal_m==tm_mo&&dn==tm_d;
        if(today)circle_ex(cx+cw/2,cy+20,18,0,T.accent,255);else if(cur&&inr(mouse_x,mouse_y,cx,cy,cw,40))circle_ex(cx+cw/2,cy+20,18,0,T.hover,200);
        char t[4];ksnprintf(t,4,"%d",dn);draw_text(cx+(cw-text_w(t,1))/2,cy+16,t,today?(lum(T.accent)>150?0x0B1020:0xFFFFFF):(cur?T.text:T.dim),1);}
}
static void tile(int x,int y,int w,int h,int on,int icon,const char*t,const char*sub){
    int hv=inr(mouse_x,mouse_y,x,y,w,h);u32 bg=on?T.accent:(hv?T.hover:T.win2);rrect(x,y,w,h,10,bg,on?255:210);
    u32 fg=on?(lum(T.accent)>150?0x0B1020:0xFFFFFF):T.text;icon_color(fg,bg);draw_icon(icon,x+10,y+10,22);draw_text(x+10,y+h-30,t,fg,1);draw_text(x+10,y+h-17,sub,on?fg:T.dim,1);
}
static void slider(int x,int y,int w,int pct,int icon){icon_color(T.text,0);draw_icon(icon,x,y-8,22);int tx=x+36,tw=w-36;rrect(tx,y+2,tw,6,3,T.field,255);rrect(tx,y+2,MAX(6,tw*pct/100),6,3,T.accent,255);circle_ex(tx+tw*pct/100,y+5,9,0,0xFFFFFF,255);circle_ex(tx+tw*pct/100,y+5,9,2,T.accent,255);}
static void draw_qs(int x,int y,int w,int h){
    acrylic(x,y,w,h,14,T.tint,g_dark?205:225);int tw=(w-48-16)/3;char b[48];
    tile(x+24,y+20,tw,66,!g_air,IC_WIFI,"Network",g_air?"Off":"No driver");
    tile(x+24+tw+8,y+20,tw,66,g_dark,IC_MOON,"Dark mode",g_dark?"On":"Off");
    tile(x+24+2*(tw+8),y+20,tw,66,g_night,IC_NIGHT,"Night light",g_night?"On":"Off");
    tile(x+24,y+94,tw,66,g_anim,IC_FOCUS,"Animations",g_anim?"On":"Off");
    tile(x+24+tw+8,y+94,tw,66,g_air,IC_AIR,"Airplane",g_air?"On":"Off");
    tile(x+24+2*(tw+8),y+94,tw,66,g_focus,IC_FOCUS,"Focus",g_focus?"On":"Off");
    slider(x+24,y+196,w-48,g_bright,IC_SUN);slider(x+24,y+246,w-48,g_vol,IC_SPEAKER);
    draw_text_b(x+24,y+288,"Accent color",T.text,1);
    for(int i=0;i<8;i++){int cx=x+40+i*44,cy=y+326;circle_ex(cx,cy,15,0,accents[i],255);if(accents[i]==g_accent)circle_ex(cx,cy,19,2,T.text,255);}
    ksnprintf(b,48,"%s",nic_name());draw_text(x+24,y+364,b,T.text,1);draw_text(x+24,y+380,"No driver loaded - link status unavailable",T.dim,1);
    fill_rect_a(x,y+h-64,w,1,T.dim,60);icon_color(T.text,0);draw_icon(IC_BATT,x+22,y+h-44,24);draw_text(x+56,y+h-42,"100%",T.text,1);draw_text(x+56,y+h-28,"Plugged in",T.dim,1);
    int hv=inr(mouse_x,mouse_y,x+w-160,y+h-52,136,40);rrect(x+w-160,y+h-52,136,40,8,hv?T.hover:T.win2,220);draw_text(x+w-160+(136-text_w("All settings",1))/2,y+h-36,"All settings",T.text,1);
}
static void draw_widgets(int x,int y,int w,int h){
    acrylic(x,y,w,h,14,T.tint,g_dark?205:225);char b[48];int cw=w-32;
    rrect(x+16,y+16,cw,110,12,T.win2,200);fmt_time(b,0);draw_text(x+34,y+34,b,T.text,3);
    ksnprintf(b,48,"%s, %s %d",dow_n[dow(tm_y,tm_mo,tm_d)],mon_n[tm_mo-1],tm_d);draw_text(x+34,y+76,b,T.dim,1);
    u32 up=ticks/100;ksnprintf(b,48,"Uptime %dh %dm %ds",up/3600,(up/60)%60,up%60);draw_text(x+34,y+96,b,T.dim,1);
    rrect(x+16,y+138,cw,200,12,T.win2,200);draw_text_b(x+34,y+152,"System",T.text,1);
    int mp=pct(heap_used(),heap_total());
    gauge(x+16+cw/4,y+244,40,cpu_pct,T.accent,T.dim);gauge(x+16+3*cw/4,y+244,40,mp,0xF472B6,T.dim);
    ksnprintf(b,48,"%d%%",cpu_pct);draw_text(x+16+cw/4-text_w(b,1)/2,y+240,b,T.text,1);ksnprintf(b,48,"%d%%",mp);draw_text(x+16+3*cw/4-text_w(b,1)/2,y+240,b,T.text,1);
    draw_text(x+16+cw/4-12,y+304,"CPU",T.dim,1);draw_text(x+16+3*cw/4-12,y+304,"RAM",T.dim,1);
    int pids[40];char nm[40][24];int np=proc_list(pids,nm,40);
    rrect(x+16,y+350,cw,70,12,T.win2,200);ksnprintf(b,48,"%d processes  -  %d desktop%s",np,ndesk,ndesk>1?"s":"");draw_text(x+34,y+366,b,T.text,1);
    ksnprintf(b,48,"RAM %d MB  -  %dx%d",mem_total_kb/1024,SW,SH);draw_text(x+34,y+388,b,T.dim,1);
    rrect(x+16,y+432,cw,MAX(40,MIN(110,h-448)),12,T.win2,200);draw_text_b(x+34,y+446,"Comet tip",T.text,1);
    const char*tips[3]={"Hover the maximize button for snap layouts.","Win+Tab shows all desktops. Ctrl+Win+Arrows switches.","Drag a window to a screen edge to snap it."};
    draw_text(x+34,y+466,tips[(ticks/1000)%3],T.dim,1);
}

/* ------------------------------------------------ overlays ------------- */
static void tv_card(int i,int n,int*x,int*y,int*w,int*h){
    int cols=n<=1?1:(n<=4?2:(n<=9?3:4));int cwid=MIN(300,(SW-80)/cols-24),chh=cwid*62/100;int rows=(n+cols-1)/cols;
    int tot=cols*(cwid+24)-24,x0=(SW-tot)/2,y0=MAX(190,(SH-TB_H-rows*(chh+30))/2+60);
    *w=cwid;*h=chh;*x=x0+(i%cols)*(cwid+24);*y=y0+(i/cols)*(chh+30);
}
static int tv_list(int*o){int n=0;for(int k=0;k<nz;k++){win_t*w=&wins[zord[k]];if(w->used&&w->desktop==cur_desk&&!w->closing)o[n++]=zord[k];}return n;}
static void draw_taskview(void){
    fill_rect_a(0,0,SW,SH-TB_H,g_dark?0x05070F:0x20263A,g_dark?190:150);
    for(int d=0;d<=ndesk&&d<5;d++){int tw=176,x0=(SW-(MIN(ndesk+1,5)*(tw+16)-16))/2,x=x0+d*(tw+16),y=34;
        if(d==ndesk){if(ndesk>=4)break;rrect(x,y,tw,100,10,T.win2,150);draw_text(x+tw/2-4,y+42,"+",T.text,2);draw_text(x+tw/2-44,y+74,"New desktop",T.dim,1);continue;}
        rmica(x,y,tw,100,10,T.tint,120);if(d==cur_desk)rframe(x,y,tw,100,10,T.accent,255),rframe(x+1,y+1,tw-2,98,9,T.accent,255);
        for(int k=0;k<nz;k++){win_t*w=&wins[zord[k]];if(!w->used||w->desktop!=d||w->minimized)continue;rrect(x+w->x*tw/SW,y+w->y*100/SH,MAX(6,w->w*tw/SW),MAX(5,w->h*100/SH),2,T.win,230);}
        char b[24];ksnprintf(b,24,"Desktop %d",d+1);draw_text(x+8,y+106,b,T.text,1);
    }
    int l[MAXW],n=tv_list(l);if(!n)draw_text(SW/2-100,SH/2-20,"No windows on this desktop",T.dim,1);
    for(int i=0;i<n;i++){int x,y,w,h;tv_card(i,n,&x,&y,&w,&h);win_t*wn=&wins[l[i]];int hv=inr(mouse_x,mouse_y,x,y,w,h);
        shadow(x,y,w,h,10,8);rrect(x,y,w,h,10,T.win,255);fill_grad(x+1,y+1,w-2,26,blend(T.accent,T.win,200),T.win);rframe(x,y,w,h,10,hv?T.accent:T.border,hv?255:120);
        draw_icon(wn->app,x+8,y+5,18);draw_text(x+32,y+10,wn->title,T.text,1);draw_icon(wn->app,x+w/2-24,y+h/2-14,48);
        if(hv){icon_color(T.text,0);circle_ex(x+w-16,y+14,9,0,T.bad,255);icon_color(0xFFFFFF,0);draw_icon(IC_CLOSE,x+w-22,y+8,12);}}
}
static void draw_alttab(void){
    int n=naltl,iw=124,w=n*iw+24,h=130,x=(SW-w)/2,y=(SH-TB_H-h)/2;acrylic(x,y,w,h,14,T.tint,215);
    for(int i=0;i<n;i++){win_t*wn=&wins[altlist[i]];int ix=x+12+i*iw;if(i==alt_sel){rrect(ix,y+10,iw-8,110,10,T.hover,240);rframe(ix,y+10,iw-8,110,10,T.accent,255);}
        draw_icon(wn->app,ix+(iw-8-48)/2,y+24,48);char t[16];scopy(t,wn->title,14);draw_text(ix+(iw-8-text_w(t,1))/2,y+88,t,T.text,1);}
}
static void draw_ctx(void){
    int w=210,h=6*30+12;int x=MIN(ctx_x,SW-w-4),y=MIN(ctx_y,SH-TB_H-h-4);acrylic(x,y,w,h,10,T.tint,230);
    for(int i=0;i<6;i++){if(inr(mouse_x,mouse_y,x+6,y+6+i*30,w-12,28))rrect(x+6,y+6+i*30,w-12,28,6,T.hover,230);draw_text(x+18,y+16+i*30,ctx_items[i],T.text,1);}
}
static void draw_snapfly(win_t*w){
    int bx=w->x+w->w-92+23,x=CLAMP(bx-116,6,SW-238),y=w->y+TH+2;snap_hz=-1;
    acrylic(x,y,232,62,10,T.tint,235);
    static const int lay[3][4]={{1,2,0,0},{7,8,0,0},{3,4,5,6}};
    for(int l=0;l<3;l++){int lx=x+8+l*74;
        for(int k=0;k<4&&lay[l][k];k++){int z=lay[l][k],zx,zy,zw,zh;zone_rect(z,&zx,&zy,&zw,&zh);int wx,wy,ww,wh;wa_rect(&wx,&wy,&ww,&wh);
            int rx=lx+zx*66/ww,ry=y+8+zy*46/wh,rw=MAX(8,zw*66/ww-2),rh=MAX(8,zh*46/wh-2);int hv=inr(mouse_x,mouse_y,rx,ry,rw,rh);
            if(hv)snap_hz=z;rrect(rx,ry,rw,rh,3,hv?T.accent:T.dim,hv?255:150);}}
}
static void draw_toasts(void){
    for(int i=0;i<3;i++){if(!toasts[i].msg[0])continue;int age=ticks-toasts[i].t0;if(age>400){toasts[i].msg[0]=0;continue;}
        int w=text_w(toasts[i].msg,1)+60,x=SW-w-14+(age<15?(15-age)*(w+20)/15:0),y=SH-TB_H-16-(i+1)*58+(i?0:0);
        if(fly==FL_QS||fly==FL_CAL)y-=0;acrylic(x,y,w,48,10,T.tint,225);rrect(x,y+8,3,32,1,T.accent,255);draw_text(x+18,y+20,toasts[i].msg,T.text,1);}
}
static void draw_desktop_icons(void){
    for(int i=0;i<6;i++){int x=16,y=16+i*98;if(i==dsel)rrect(x,y,86,90,8,T.accent,70),rframe(x,y,86,90,8,T.accent,160);
        draw_icon(dicons[i],x+15,y+8,56);const char*n=apps[dicons[i]].name;int tw=text_w(n,1);draw_text(x+43-tw/2+1,y+69,n,0x000000,1);draw_text(x+43-tw/2,y+68,n,0xFFFFFF,1);}
}

/* ------------------------------------------------ compose -------------- */
static win_t*win_at(int x,int y){
    for(int k=nz-1;k>=0;k--){win_t*w=&wins[zord[k]];if(!vis(w))continue;int m=w->max==1?0:5;if(inr(x,y,w->x-m,w->y-m,w->w+2*m,w->h+2*m))return w;}
    return 0;
}
static int shell_covers(int x,int y){
    if(y>=SH-TB_H)return 1;if(fly){int fx,fy,fw,fh;fly_rect(&fx,&fy,&fw,&fh);if(inr(x,y,fx,fy,fw,fh))return 1;}return taskview||alttab||ctx_open;
}
static void compose(void){
    u64 t0=rdtsc();clip_reset();tb_layout();
    hover_w=shell_covers(mouse_x,mouse_y)?0:win_at(mouse_x,mouse_y);
    memcpy32(bb,wall,SW*SH);draw_desktop_icons();
    if(dsk_from>=0){int p=ease(ticks-dsk_t0,ANIM_T+4),dir=dsk_to>dsk_from?1:-1;draw_wins(dsk_from,-dir*SW*p/1000);draw_wins(dsk_to,dir*SW*(1000-p)/1000);if(p>=1000)dsk_from=-1;}
    else draw_wins(cur_desk,0);
    if(snap_prev){int x,y,w,h;zone_rect(snap_prev,&x,&y,&w,&h);rrect(x,y,w,h,10,T.accent,60);rframe(x,y,w,h,10,T.accent,220);}
    if(taskview)draw_taskview();
    draw_taskbar();
    if(fly){int x,y,w,h;fly_rect(&x,&y,&w,&h);int p=ease(ticks-fly_t0,12),oy=(1000-p)*36/1000;y+=oy;
        if(fly==FL_START)draw_start(x,y,w,h);else if(fly==FL_CAL)draw_cal(x,y,w,h);else if(fly==FL_QS)draw_qs(x,y,w,h);else draw_widgets(x,y,w,h);}
    if(snap_cand>=0&&wins[snap_cand].used&&ticks-snap_t0>=35&&!taskview)draw_snapfly(&wins[snap_cand]);else snap_hz=-1;
    if(alttab)draw_alttab();if(ctx_open)draw_ctx();draw_toasts();
    if(g_bright<100||g_night)post_fx(g_bright,g_night);
    comp_cyc+=rdtsc()-t0;
}

/* ------------------------------------------------ desktops / procs ----- */
static u32 sd_mask;
static void switch_desktop(int d){
    if(d<0||d>=ndesk||d==cur_desk)return;dsk_from=cur_desk;dsk_to=d;dsk_t0=ticks;cur_desk=d;focus_next();dirty=1;
}
static void new_desktop(void){if(ndesk>=4)return;ndesk++;switch_desktop(ndesk-1);}
static void show_desktop(void){
    int any=0;for(int i=0;i<MAXW;i++)if(wins[i].used&&wins[i].desktop==cur_desk&&!wins[i].minimized&&!wins[i].closing)any=1;
    for(int i=0;i<MAXW;i++){win_t*w=&wins[i];if(!w->used||w->desktop!=cur_desk||w->closing)continue;
        if(any&&!w->minimized){w->minimized=1;sd_mask|=1u<<i;anim_start(w,3);}
        else if(!any&&(sd_mask&(1u<<i))){w->minimized=0;sd_mask&=~(1u<<i);anim_start(w,4);}}
    if(any)focus_i=-1;else focus_next();
}
static const char*sysn[5]={"kernel","compositor","input-daemon","ramfs","shell"};
int proc_list(int*pids,char names[][24],int max){
    int n=0;for(int i=0;i<5&&n<max;i++){pids[n]=i+1;scopy(names[n],sysn[i],24);n++;}
    for(int i=0;i<MAXW&&n<max;i++)if(wins[i].used&&!wins[i].closing){pids[n]=wins[i].pid;scopy(names[n],apps[wins[i].app].name,24);n++;}
    return n;
}
int proc_info(int pid,u32*cpu,u32*mem){
    if(pid>=1&&pid<=5){*cpu=pid==2?comp_pct*10:0;*mem=pid==1?heap_used()/1024:pid==2?(u32)SW*SH*16/1024:pid==4?fs_used()/1024:128;return 1;}
    for(int i=0;i<MAXW;i++)if(wins[i].used&&wins[i].pid==pid){*cpu=wins[i].cpu;*mem=96+((u32)wins[i].w*wins[i].h/1024)+wins[i].mem;return 1;}
    return 0;
}
int proc_kill(int pid){if(pid>=1&&pid<=5)return -1;for(int i=0;i<MAXW;i++)if(wins[i].used&&wins[i].pid==pid&&!wins[i].closing){win_close(&wins[i]);return 1;}return 0;}
static void save_cfg(void){cfg_theme=g_dark;for(int i=0;i<8;i++)if(accents[i]==g_accent)cfg_accent=i;cfg_save();}
int ui_res_set(int w,int h){
    if(!gfx_setmode(w,h))return 0;
    mouse_clamp();for(int i=0;i<MAXW;i++)if(wins[i].used){win_t*x=&wins[i];x->w=MIN(x->w,SW-8);x->h=MIN(x->h,SH-TB_H-8);x->x=CLAMP(x->x,0,SW-x->w);x->y=CLAMP(x->y,0,SH-TB_H-x->h);if(x->max)x->max=0;}
    fly=0;taskview=0;theme_apply();
    int idx=SW==1024?0:SW==1280?1:SW==1600?2:3;cfg_res=idx;save_cfg();return 1;
}

/* ------------------------------------------------ input handling ------- */
static struct{int mode,win,ox,oy,edges,sx,sy,sw,sh,mx0,my0,slider;}drag;
static int edge_flags(win_t*w,int x,int y){int f=0;if(x<w->x+5)f|=1;if(x>=w->x+w->w-5)f|=2;if(y<w->y+5)f|=4;if(y>=w->y+w->h-5)f|=8;return f;}
static int edge_cursor(int f){if(f==1||f==2)return 1;if(f==4||f==8)return 2;if(f==5||f==10)return 3;if(f==6||f==9)return 4;return 0;}
static void alttab_begin(int dir){
    naltl=0;for(int k=nz-1;k>=0;k--){win_t*w=&wins[zord[k]];if(w->used&&w->desktop==cur_desk&&!w->closing)altlist[naltl++]=zord[k];}
    if(!naltl)return;alttab=1;alt_sel=naltl>1?(dir>0?1:naltl-1):0;
}
static void app_click(int app){
    win_t*f=0;for(int k=nz-1;k>=0&&!f;k--)if(wins[zord[k]].used&&wins[zord[k]].app==app&&!wins[zord[k]].closing)f=&wins[zord[k]];
    if(!f){win_open(app);return;}
    if(widx(f)==focus_i&&!f->minimized&&f->desktop==cur_desk)win_minimize(f);else win_focus(f);
}
static void start_key(int key){
    if(key=='\n'){if(sqlen&&nsres)launch(sres[CLAMP(ssel,0,nsres-1)]);return;}
    if(key==K_DOWN&&ssel<nsres-1)ssel++;else if(key==K_UP&&ssel>0)ssel--;
    else if(key=='\b'){if(sqlen)sq[--sqlen]=0;ssel=0;}
    else if(key>=32&&key<127&&sqlen<38){sq[sqlen++]=key;sq[sqlen]=0;ssel=0;}
}
static void handle_key(int key,int mods){
    if(mods&M_WIN){
        switch(key){
        case 'd':show_desktop();return;case '\t':taskview=!taskview;fly=0;return;
        case K_LEFT:if(mods&M_CTRL)switch_desktop(cur_desk-1);else if(focus_i>=0)win_snap(&wins[focus_i],1);return;
        case K_RIGHT:if(mods&M_CTRL)switch_desktop(cur_desk+1);else if(focus_i>=0)win_snap(&wins[focus_i],2);return;
        case K_UP:if(focus_i>=0)win_snap(&wins[focus_i],9);return;
        case K_DOWN:if(focus_i>=0){if(wins[focus_i].max)win_restore(&wins[focus_i]);else win_minimize(&wins[focus_i]);}return;
        case 'e':win_open(APP_FILES);return;case 'i':win_open(APP_SET);return;case 't':win_open(APP_TERM);return;
        case 's':if(fly!=FL_START)fly_open(FL_START);return;case 'a':fly_open(FL_QS);return;case 'w':fly_open(FL_WIDGETS);return;case 'n':fly_open(FL_CAL);return;
        case 'm':for(int i=0;i<MAXW;i++)if(wins[i].used&&wins[i].desktop==cur_desk&&!wins[i].minimized)win_minimize(&wins[i]);return;
        case 'd'-100:return;
        }
        if(key>='1'&&key<='9'){tb_layout();int k=3+(key-'1');if(k<ntb)app_click(tbs[k].app);return;}
        return;
    }
    if((mods&M_ALT)&&key=='\t'){if(alttab)alt_sel=(alt_sel+((mods&M_SHIFT)?naltl-1:1))%MAX(naltl,1);else alttab_begin(1);return;}
    if((mods&M_ALT)&&key==K_F4){if(focus_i>=0)win_close(&wins[focus_i]);return;}
    if((mods&M_CTRL)&&(mods&M_SHIFT)&&key==27){win_open(APP_TASK);return;}
    if((mods&M_CTRL)&&(mods&M_ALT)&&key==K_DEL){win_open(APP_TASK);return;}
    if(key==27){if(ctx_open){ctx_open=0;return;}if(taskview){taskview=0;return;}if(fly){fly=0;return;}}
    if(taskview||alttab)return;
    if(fly==FL_START){start_key(key);return;}
    if(key==K_F11&&focus_i>=0){win_togglemax(&wins[focus_i]);return;}
    if(focus_i>=0&&wins[focus_i].used&&apps[wins[focus_i].app].key){win_t*w=&wins[focus_i];u64 t0=rdtsc();apps[w->app].key(w,key,mods);w->cycles+=rdtsc()-t0;}
}
static void tb_click(int x,int y){
    tb_layout();int y0=SH-TB_H;
    for(int i=0;i<ntb;i++){tbi*t=&tbs[i];if(inr(x,y,t->x,t->y,44,44)){
        if(t->kind<=1){taskview=0;fly_open(FL_START);}else if(t->kind==2){taskview=!taskview;fly=0;}else{taskview=0;fly=0;app_click(t->app);}return;}}
    if(inr(x,y,8,y0+6,150,40)){taskview=0;fly_open(FL_WIDGETS);}
    else if(inr(x,y,SW-218,y0+6,108,40)){taskview=0;fly_open(FL_QS);}
    else if(inr(x,y,SW-104,y0+6,98,40)){taskview=0;fly_open(FL_CAL);}
    else if(x>=SW-6)show_desktop();
}
static void set_slider(int which,int x,int fx,int fw){
    int v=(x-(fx+24+36))*100/(fw-48-36);v=CLAMP(v,which?0:15,100);if(which)g_vol=v;else g_bright=v;
}
static void fly_click(int x,int y){
    int X,Y,W,H;fly_rect(&X,&Y,&W,&H);
    if(fly==FL_START){
        if(pwr_open){int px=X+W-170,py=Y+H-64-76;if(inr(x,y,px+6,py+6,138,28))poweroff();if(inr(x,y,px+6,py+36,138,28))reboot();pwr_open=0;if(inr(x,y,px,py,150,70))return;}
        if(inr(x,y,X+W-64,Y+H-52,40,40)){pwr_open=!pwr_open;return;}
        if(sqlen){for(int i=0;i<nsres;i++)if(inr(x,y,X+20,Y+96+i*50,W-40,46)){launch(sres[i]);return;}return;}
        int pl[APP_COUNT],n=pinned_list(pl),cw=(W-48)/6;
        for(int i=0;i<n&&i<18;i++){int cx,cy;start_cell(i,X,Y,W,&cx,&cy);if(inr(x,y,cx,cy,cw,80)){launch(pl[i]);return;}}
        int ry=Y+100+3*84+4;for(int i=0;i<nrecent;i++){int rx=X+24+(i%2)*cw*3,rry=ry+24+(i/2)*48;if(inr(x,y,rx,rry,cw*3-6,44)){launch(1000+recent[i]);return;}}
    }else if(fly==FL_CAL){
        if(inr(x,y,X+W-96,Y+112,32,28)){if(--cal_m<1){cal_m=12;cal_y--;}}
        if(inr(x,y,X+W-56,Y+112,32,28)){if(++cal_m>12){cal_m=1;cal_y++;}}
    }else if(fly==FL_QS){
        int tw=(W-48-16)/3;
        for(int i=0;i<6;i++){int tx=X+24+(i%3)*(tw+8),ty=Y+20+(i/3)*74;if(!inr(x,y,tx,ty,tw,66))continue;
            if(i==0||i==4){g_air=!g_air;if(g_air)toast("Airplane mode on");}
            else if(i==1){g_dark=!g_dark;theme_apply();save_cfg();}else if(i==2)g_night=!g_night;else if(i==3)g_anim=!g_anim;else g_focus=!g_focus;}
        if(y>=Y+185&&y<Y+215&&x>=X+24){drag.mode=4;drag.slider=0;set_slider(0,x,X,W);}
        if(y>=Y+235&&y<Y+265&&x>=X+24){drag.mode=4;drag.slider=1;set_slider(1,x,X,W);}
        for(int i=0;i<8;i++){int dx=x-(X+40+i*44),dy=y-(Y+326);if(dx*dx+dy*dy<=17*17){g_accent=accents[i];theme_apply();save_cfg();}}
        if(inr(x,y,X+W-160,Y+H-52,136,40)){fly=0;win_open(APP_SET);}
    }
}
static void tv_click(int x,int y){
    for(int d=0;d<=ndesk&&d<5;d++){int tw=176,x0=(SW-(MIN(ndesk+1,5)*(tw+16)-16))/2,dx=x0+d*(tw+16);
        if(inr(x,y,dx,34,tw,100)){if(d==ndesk)new_desktop();else{switch_desktop(d);}taskview=0;if(d==ndesk)taskview=1;return;}}
    int l[MAXW],n=tv_list(l);
    for(int i=0;i<n;i++){int cx,cy,cw,ch;tv_card(i,n,&cx,&cy,&cw,&ch);if(inr(x,y,cx,cy,cw,ch)){
        if(inr(x,y,cx+cw-26,cy+4,20,20))win_close(&wins[l[i]]);else{win_focus(&wins[l[i]]);taskview=0;}return;}}
    taskview=0;
}
static int last_ct,last_cx,last_cy,last_cb;
static void handle_mbtn(int btn,int down,int x,int y){
    if(!down){
        if(drag.mode==1&&snap_prev&&drag.win>=0)win_snap(&wins[drag.win],snap_prev);
        if(drag.mode==3&&drag.win>=0&&wins[drag.win].used&&apps[wins[drag.win].app].release){win_t*w=&wins[drag.win];apps[w->app].release(w,x-w->x,y-w->y-TH);}
        snap_prev=0;drag.mode=0;return;
    }
    int dbl=(ticks-last_ct<40&&ABS(x-last_cx)<5&&ABS(y-last_cy)<5&&btn==last_cb);last_ct=ticks;last_cx=x;last_cy=y;last_cb=btn;if(dbl)last_ct=0;
    if(ctx_open){int w=210,h=6*30+12,cx=MIN(ctx_x,SW-w-4),cy=MIN(ctx_y,SH-TB_H-h-4);ctx_open=0;
        for(int i=0;i<6;i++)if(inr(x,y,cx+6,cy+6+i*30,w-12,28)){
            if(i==0)win_open(APP_TERM);else if(i==1){char nm[32];fs_unique(fs_docs,"New note.txt",nm);int n=fs_create(fs_docs,nm,0);if(n>0)win_open_arg(APP_NOTES,n);}
            else if(i==2){g_dark=!g_dark;theme_apply();save_cfg();}else if(i==3||i==4)win_open(APP_SET);else win_open(APP_TASK);}
        return;}
    if(alttab)return;
    if(taskview&&y<SH-TB_H){tv_click(x,y);return;}
    if(y>=SH-TB_H){tb_click(x,y);return;}
    if(fly){int X,Y,W,H;fly_rect(&X,&Y,&W,&H);if(inr(x,y,X,Y,W,H)){fly_click(x,y);return;}fly=0;if(btn==1)return;}
    if(snap_cand>=0&&ticks-snap_t0>=35&&snap_hz>=0){win_snap(&wins[snap_cand],snap_hz);snap_cand=-1;return;}
    win_t*w=win_at(x,y);
    if(w){
        int ed=w->max==1?0:edge_flags(w,x,y);
        if(widx(w)!=focus_i||zord[nz-1]!=widx(w))win_focus(w);
        if(btn!=1&&y<w->y+TH)return;
        if(ed&&btn==1){drag.mode=2;drag.win=widx(w);drag.edges=ed;drag.sx=w->x;drag.sy=w->y;drag.sw=w->w;drag.sh=w->h;drag.mx0=x;drag.my0=y;w->max=0;return;}
        if(y<w->y+TH){
            if(x>=w->x+w->w-46){win_close(w);return;}
            if(x>=w->x+w->w-92){win_togglemax(w);return;}
            if(x>=w->x+w->w-138){win_minimize(w);return;}
            if(dbl){win_togglemax(w);return;}
            drag.mode=1;drag.win=widx(w);drag.ox=x-w->x;drag.oy=y-w->y;return;
        }
        int lx=x-w->x,ly=y-w->y-TH,id=-1;
        for(int i=w->nhits-1;i>=0;i--){hit_t*h=&w->hits[i];if(inr(lx,ly,h->x,h->y,h->w,h->h)){id=h->id;break;}}
        u64 t0=rdtsc();
        if(id>=0&&btn==1){if(apps[w->app].action)apps[w->app].action(w,id,dbl);}
        else if(apps[w->app].click)apps[w->app].click(w,lx,ly,btn,dbl);
        w->cycles+=rdtsc()-t0;
        if(btn==1){drag.mode=3;drag.win=widx(w);}
        return;
    }
    if(btn==2){ctx_open=1;ctx_x=x;ctx_y=y;dsel=-1;return;}
    dsel=-1;for(int i=0;i<6;i++)if(inr(x,y,16,16+i*98,86,90)){dsel=i;if(dbl)win_open(dicons[i]);}
}
static void handle_move(void){
    int x=mouse_x,y=mouse_y;int kind=0;dirty=1;
    if(drag.mode==2)kind=edge_cursor(drag.edges);
    else if(drag.mode==0&&!shell_covers(x,y)){win_t*w=win_at(x,y);if(w&&w->max!=1){int f=edge_flags(w,x,y);if(f&&!(y>=w->y+5&&y<w->y+TH&&x<w->x+w->w-5&&x>=w->x+5))kind=edge_cursor(f);}}
    cursor_set(kind);
    if(drag.mode==1&&drag.win>=0){win_t*w=&wins[drag.win];
        if(w->max){int nw=w->rw,nh=w->rh;int ox=(x-w->x)*nw/MAX(w->w,1);w->w=nw;w->h=nh;w->x=x-ox;w->max=0;drag.ox=ox;drag.oy=y-w->y;}
        w->x=x-drag.ox;w->y=CLAMP(y-drag.oy,0,SH-TB_H-TH);
        snap_prev=0;int ah=SH-TB_H;
        if(y<=0)snap_prev=9;else if(x<=0)snap_prev=y<80?3:(y>ah-80?5:1);else if(x>=SW-1)snap_prev=y<80?4:(y>ah-80?6:2);
    }else if(drag.mode==2&&drag.win>=0){
        win_t*w=&wins[drag.win];int dx=x-drag.mx0,dy=y-drag.my0,nx=drag.sx,ny=drag.sy,nw=drag.sw,nh=drag.sh,mw=apps[w->app].minw,mh=apps[w->app].minh;
        if(drag.edges&1){nw=drag.sw-dx;nx=drag.sx+dx;}if(drag.edges&2)nw=drag.sw+dx;
        if(drag.edges&4){nh=drag.sh-dy;ny=drag.sy+dy;}if(drag.edges&8)nh=drag.sh+dy;
        if(nw<mw){if(drag.edges&1)nx-=mw-nw;nw=mw;}if(nh<mh){if(drag.edges&4)ny-=mh-nh;nh=mh;}
        if(ny<0){nh+=ny;ny=0;}w->x=nx;w->y=ny;w->w=nw;w->h=nh;
    }else if(drag.mode==3&&drag.win>=0&&wins[drag.win].used&&apps[wins[drag.win].app].drag){win_t*w=&wins[drag.win];apps[w->app].drag(w,x-w->x,y-w->y-TH);}
    else if(drag.mode==4){int X,Y,W,H;fly_rect(&X,&Y,&W,&H);set_slider(drag.slider,x,X,W);}
}
static void handle_wheel(int dz){
    if(shell_covers(mouse_x,mouse_y))return;win_t*w=win_at(mouse_x,mouse_y);
    if(w&&apps[w->app].scroll){apps[w->app].scroll(w,dz);dirty=1;}
}

/* ------------------------------------------------ boot / loop ---------- */
void splash(int step){
    if(!bb)return;clip_reset();fill_grad(0,0,SW,SH,0x070B18,0x1A1038);
    circle_ex(SW/2,SH/2-40,160,0,0x4C8DFF,26);circle_ex(SW/2,SH/2-40,90,0,0x8B5CF6,34);
    draw_logo(SW/2,SH/2-50,90,0x7FB0FF);draw_text(SW/2-text_w("Comet OS",3)/2,SH/2+30,"Comet OS",0xFFFFFF,3);
    int bw=240,bx=(SW-bw)/2,by=SH/2+90;rrect(bx,by,bw,4,2,0x2A3050,255);rrect(bx,by,bw*(step+1)/4,4,2,0x7FB0FF,255);
    draw_text(SW/2-text_w("Starting up...",1)/2,by+16,"Starting up...",0x9AA0B4,1);gfx_present();
}
void wm_init(void){
    rtc_read(&tm_h,&tm_m,&tm_s,&tm_d,&tm_mo,&tm_y);cal_m=tm_mo;cal_y=tm_y;
    g_dark=cfg_theme;g_accent=accents[cfg_accent&7];
    static const int rw[4]={1024,1280,1600,1920},rh[4]={768,800,900,1080};
    if(g_installed&&bga_ok&&(rw[cfg_res&3]!=SW||rh[cfg_res&3]!=SH))gfx_setmode(rw[cfg_res&3],rh[cfg_res&3]);
    theme_apply();mouse_x=SW/2;mouse_y=SH/2;last_tsc=rdtsc();
    for(int i=0;i<APP_COUNT;i++)if(((cfg_startup>>i)&1)&&apps[i].installed)win_open(i);
    toast(g_installed?"Welcome back to Comet OS":"Live session - open Comet Setup to install");
}
static void tick_update(void){
    static u32 last_sec;
    if(ticks-last_sec>=100){last_sec=ticks;rtc_read(&tm_h,&tm_m,&tm_s,&tm_d,&tm_mo,&tm_y);
        u64 now=rdtsc(),tot=now-last_tsc;last_tsc=now;u32 t=(u32)(tot>>14);if(!t)t=1;
        int busy=100-(int)((u32)(idle_cyc>>14)*100/t);cpu_pct=CLAMP(busy,0,100);comp_pct=CLAMP((int)((u32)(comp_cyc>>14)*100/t),0,100);
        for(int i=0;i<MAXW;i++){wins[i].cpu=(u32)(wins[i].cycles>>14)*1000/t;wins[i].cycles=0;}
        idle_cyc=0;comp_cyc=0;for(int i=0;i<59;i++)cpu_hist[i]=cpu_hist[i+1];cpu_hist[59]=cpu_pct;dirty=1;}
    anims_update();
    if(dsk_from>=0||(fly&&ticks-fly_t0<14)||toasts[0].msg[0]||toasts[1].msg[0]||toasts[2].msg[0])dirty=1;
    int idx=-1;
    if(!shell_covers(mouse_x,mouse_y)){win_t*h=win_at(mouse_x,mouse_y);if(h&&inr(mouse_x,mouse_y,h->x+h->w-92,h->y,46,TH))idx=widx(h);}
    if(snap_cand>=0&&ticks-snap_t0>=35){win_t*w=&wins[snap_cand];int fx=CLAMP(w->x+w->w-92+23-116,6,SW-238);if(inr(mouse_x,mouse_y,fx,w->y+TH,232,64))idx=snap_cand;}
    if(idx!=snap_cand){snap_cand=idx;snap_t0=ticks;dirty=1;}else if(idx>=0&&ticks-snap_t0==35)dirty=1;
    for(int i=0;i<MAXW;i++){win_t*w=&wins[i];if(w->used&&!w->closing&&apps[w->app].tick){u64 t0=rdtsc();apps[w->app].tick(w);w->cycles+=rdtsc()-t0;}}
}
void wm_run(void){
    u32 last_tick=ticks,last_frame=0;
    for(;;){
        event_t e;
        while(ev_pop(&e)){dirty=1;
            switch(e.type){
            case EV_KEY:handle_key(e.a,e.b);break;
            case EV_MBTN:handle_mbtn(e.a,e.b,e.c>>16,e.c&0xFFFF);break;
            case EV_WHEEL:handle_wheel(e.a);break;
            case EV_WINTAP:if(!taskview){fly_open(FL_START);}break;
            case EV_ALTUP:if(alttab){alttab=0;if(naltl)win_focus(&wins[altlist[CLAMP(alt_sel,0,naltl-1)]]);}break;
            }
        }
        if(mouse_moved){mouse_moved=0;handle_move();cursor_dirty=1;}
        if(ticks!=last_tick){last_tick=ticks;tick_update();}
        if(inst.state==1){inst_step();dirty=1;}
        if(dirty&&ticks!=last_frame){dirty=0;last_frame=ticks;compose();gfx_present();cursor_show(mouse_x,mouse_y);cursor_dirty=0;}
        else if(cursor_dirty){cursor_dirty=0;cursor_hide();cursor_show(mouse_x,mouse_y);}
        else{u64 t=rdtsc();hlt();idle_cyc+=rdtsc()-t;}
    }
}
