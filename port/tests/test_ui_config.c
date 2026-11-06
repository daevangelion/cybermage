#define main cybermage_main
#define snd_close config_checks
#include "../src/main.c"
#undef main
#undef snd_close
#include <assert.h>

void snd_close(void);
static uint8_t expected_config[172];
static unsigned launch;
static char test_config_path[PATH_BYTES];

static void codec_checks(const uint8_t *original)
{
    ui_config c,before;
    uint8_t encoded[UI_CONFIG_BYTES],bad[UI_CONFIG_BYTES+1];
    assert(!ui_config_decode(&c,original,UI_CONFIG_BYTES,"CMDATA.CNF"));
    assert(!ui_config_encode(&c,encoded,sizeof encoded,"CMDATA.CNF"));
    assert(!memcmp(encoded,original,sizeof encoded));
    before=c;
    assert(ui_config_decode(&c,original,UI_CONFIG_BYTES-1,"CMDATA.CNF")==-1);
    assert(cm_last_error()->offset==171 && c.difficulty==before.difficulty);
    memcpy(bad,original,UI_CONFIG_BYTES);bad[UI_CONFIG_BYTES]=0;
    assert(ui_config_decode(&c,bad,sizeof bad,"CMDATA.CNF")==-1 && cm_last_error()->offset==172);
    bad[8]=3;bad[9]=bad[10]=bad[11]=0;
    assert(ui_config_decode(&c,bad,UI_CONFIG_BYTES,"CMDATA.CNF")==-1 && cm_last_error()->offset==8);
    assert(c.difficulty==before.difficulty);
    memcpy(bad,original,UI_CONFIG_BYTES);bad[52]=128;bad[53]=bad[54]=bad[55]=0;
    assert(ui_config_decode(&c,bad,UI_CONFIG_BYTES,"CMDATA.CNF")==-1 && cm_last_error()->offset==52);
    assert(ui_config_encode(&c,encoded,sizeof encoded-1,"CMDATA.CNF")==-1);
}

static void config_word(unsigned word,uint32_t value)
{
    unsigned i;
    for(i=0;i<4;++i) expected_config[word*4+i]=(uint8_t)(value>>(i*8));
}

void config_checks(void)
{
    static const unsigned option[8]={0,3,4,5,6,7,8,9};
    static const unsigned word[8]={1,7,8,6,3,4,2,5};
    cm_file written={0};
    unsigned i;
    assert(game.scene==1 && ui.screen==UI_GAME);
    for(i=1;i<8;++i)
        assert(ui.options[option[i]]==world_wrap32(cm_u32(expected_config+word[i]*4)));
    assert(actors.difficulty==(int)cm_u32(expected_config+8));
    if(!launch) {
        cm_input key={0};
        ui.return_screen=UI_GAME;ui_open(&ui,UI_OPTIONS);
        for(i=1;i<8;++i) {
            unsigned row=option[i];
            int maximum=row==8?2:row==6||row==7?32:row==9?980:100;
            memset(&key,0,sizeof key);ui.selected=row;
            key.pressed[ui.options[row]>=maximum?75:77]=1;
            assert(menu_input(&key,20)==UI_NONE && ui.screen==UI_OPTIONS);
            config_word(word[i],(uint32_t)ui.options[row]);
        }
        memset(&key,0,sizeof key);key.pressed[1]=1;
        assert((int)menu_input(&key,21)>=0 && ui.screen==UI_GAME);
        assert(!platform_file_read(&written,test_config_path));
        assert(written.size==sizeof expected_config && !memcmp(written.data,expected_config,sizeof expected_config));
        platform_file_release(&written);
    }
    snd_close();
}

int main(int argc,char **args)
{
    cm_file original={0};
    const char *argv[]={"cybermage","--headless","--data","../orig/retail","--user",NULL,
        "--keys","../tools/scenarios/port-movement.keys","--frames","4","--bios-seed","44"};
    assert(argc==3 || argc==4);
    assert(!platform_file_read(&original,args[1]) && original.size==sizeof expected_config);
    codec_checks(original.data);
    memcpy(expected_config,original.data,sizeof expected_config);platform_file_release(&original);
    if(argc==4) {
        ui_config mouse;
        uint8_t encoded[UI_CONFIG_BYTES];
        assert(!platform_file_read(&original,args[3]) && original.size==UI_CONFIG_BYTES);
        codec_checks(original.data);
        assert(!ui_config_decode(&mouse,original.data,original.size,args[3]) && mouse.in_device==1);
        assert(!ui_config_encode(&mouse,encoded,sizeof encoded,args[3]));
        assert(!memcmp(encoded,original.data,sizeof encoded));
        platform_file_release(&original);
    }
    assert(!platform_directory(args[2]));
    assert(!make_path(test_config_path,sizeof test_config_path,args[2],"CMDATA.CNF"));
    assert(!platform_file_write(test_config_path,expected_config,sizeof expected_config));
    argv[5]=args[2];
    assert(!cybermage_main((int)(sizeof argv/sizeof argv[0]),(char **)argv));
    launch=1;
    assert(!cybermage_main((int)(sizeof argv/sizeof argv[0]),(char **)argv));
    puts("Original CMDATA.CNF: loaded before InitPC, edited through real Options, persisted and reloaded exactly");
    return 0;
}
