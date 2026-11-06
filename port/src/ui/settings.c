#include "ui.h"
#include "audio.h"

#include <string.h>

/* ReadConfigFile020560..0205CA, ReKeyDefaults052644..05277A. */
void ui_config_defaults(ui_config *c)
{
    static const uint8_t keys[UI_CONFIG_KEYS][2]={
        {0x39,1},{0x1C,5},{0x0F,1},{0x2A,1},{0x2C,1},{0x2E,1},
        {0x1D,5},{0x52,1},{0x10,1},{0x12,1},{0x13,1},{0x11,1},
        {0x2D,1},{0x32,1},{0x2F,1}
    };
    unsigned i;
    c->in_device=c->brightness=c->contrast=0;c->difficulty=1;
    c->text_delay=120;c->midi_volume=c->digital_volume=75;c->speech_volume=100;
    for(i=0;i<4;++i) c->joystick[i]=-999;
    for(i=0;i<UI_CONFIG_KEYS;++i) { c->key[i]=keys[i][0];c->mask[i]=keys[i][1]; }
}

static int config_valid(const ui_config *c,const char *path)
{
    unsigned i;
    if(c->in_device<0 || c->in_device>2)
        return cm_fail(path,4,"input device outside original range");
    if(c->difficulty<0 || c->difficulty>2)
        return cm_fail(path,8,"difficulty outside original range");
    if(c->brightness<-32 || c->brightness>32)
        return cm_fail(path,12,"brightness outside original slider range");
    if(c->contrast<-32 || c->contrast>32)
        return cm_fail(path,16,"contrast outside original slider range");
    if(c->text_delay<0 || c->text_delay>980)
        return cm_fail(path,20,"text delay outside original slider range");
    if(c->midi_volume<0 || c->midi_volume>100)
        return cm_fail(path,24,"MIDI volume outside original slider range");
    if(c->digital_volume<0 || c->digital_volume>100)
        return cm_fail(path,28,"digital volume outside original slider range");
    if(c->speech_volume<0 || c->speech_volume>100)
        return cm_fail(path,32,"speech volume outside original slider range");
    for(i=0;i<UI_CONFIG_KEYS;++i)
        if(c->key[i]>=128) return cm_fail(path,52+i*8,"binding scan outside IRQ9 table");
    return 0;
}

int ui_config_decode(ui_config *c,const uint8_t *data,uint32_t size,const char *path)
{
    ui_config v;
    unsigned i;
    if(!c || !data || size!=UI_CONFIG_BYTES)
        return cm_fail(path,size<UI_CONFIG_BYTES?size:UI_CONFIG_BYTES,"configuration must be 172 bytes");
    /* The first word is the original bLong scratch value, not a version tag. */
    v.in_device=world_wrap32(cm_u32(data+4));v.difficulty=world_wrap32(cm_u32(data+8));
    v.brightness=world_wrap32(cm_u32(data+12));v.contrast=world_wrap32(cm_u32(data+16));
    v.text_delay=world_wrap32(cm_u32(data+20));v.midi_volume=world_wrap32(cm_u32(data+24));
    v.digital_volume=world_wrap32(cm_u32(data+28));v.speech_volume=world_wrap32(cm_u32(data+32));
    for(i=0;i<4;++i) v.joystick[i]=world_wrap32(cm_u32(data+36+i*4));
    for(i=0;i<UI_CONFIG_KEYS;++i) {
        v.key[i]=cm_u32(data+52+i*8);v.mask[i]=cm_u32(data+56+i*8);
    }
    if(config_valid(&v,path)) return -1;
    *c=v;
    return 0;
}

static void config_put(uint8_t *data,uint32_t value)
{
    data[0]=(uint8_t)value;data[1]=(uint8_t)(value>>8);
    data[2]=(uint8_t)(value>>16);data[3]=(uint8_t)(value>>24);
}

int ui_config_encode(const ui_config *c,uint8_t *data,uint32_t size,const char *path)
{
    unsigned i;
    if(!c || !data || size<UI_CONFIG_BYTES)
        return cm_fail(path,size,"configuration output too small");
    if(config_valid(c,path)) return -1;
    config_put(data,0); /* WriteConfigFile020CA1 always writes bLong=0. */
    config_put(data+4,(uint32_t)c->in_device);config_put(data+8,(uint32_t)c->difficulty);
    config_put(data+12,(uint32_t)c->brightness);config_put(data+16,(uint32_t)c->contrast);
    config_put(data+20,(uint32_t)c->text_delay);config_put(data+24,(uint32_t)c->midi_volume);
    config_put(data+28,(uint32_t)c->digital_volume);config_put(data+32,(uint32_t)c->speech_volume);
    for(i=0;i<4;++i) config_put(data+36+i*4,(uint32_t)c->joystick[i]);
    for(i=0;i<UI_CONFIG_KEYS;++i) {
        config_put(data+52+i*8,c->key[i]);config_put(data+56+i*8,c->mask[i]);
    }
    return 0;
}

int ui_config_save(const ui_config *c,const char *path)
{
    uint8_t data[UI_CONFIG_BYTES];
    if(ui_config_encode(c,data,sizeof data,path)) return -1;
    return platform_file_write(path,data,sizeof data);
}

int ui_config_load(ui_config *c,const char *path)
{
    cm_file file={0};
    int result;
    if(!platform_file_exists(path)) {
        ui_config_defaults(c);
        return ui_config_save(c,path); /* Original020560 creates the missing file. */
    }
    if(platform_file_read(&file,path)) return -1;
    result=ui_config_decode(c,file.data,file.size,path);
    platform_file_release(&file);
    return result;
}

/* LoadOptVals053964 and OptOptions: GUI values are not the file's word order. */
void ui_options_from_config(cm_ui *ui)
{
    const ui_config *c=&ui->config;
    ui->options[0]=c->in_device;ui->options[3]=c->digital_volume;
    ui->options[4]=c->speech_volume;ui->options[5]=c->midi_volume;
    ui->options[6]=c->brightness;ui->options[7]=c->contrast;
    ui->options[8]=c->difficulty;ui->options[9]=c->text_delay;
}

void ui_options_to_config(cm_ui *ui)
{
    ui_config *c=&ui->config;
    c->in_device=ui->options[0];c->digital_volume=ui->options[3];
    c->speech_volume=ui->options[4];c->midi_volume=ui->options[5];
    c->brightness=ui->options[6];c->contrast=ui->options[7];
    c->difficulty=ui->options[8];c->text_delay=ui->options[9];
}

static int dac_clip(int value)
{
    return value < 0 ? 0 : value > 63 ? 63 : value;
}

/* CM_HI.EXE SetGammaBright5253C then SetGammaContrast52598. */
int ui_apply_settings(cm_ui *ui, uint8_t palette[768], const uint8_t base[768])
{
    unsigned i, c;
    int bright=ui->options[6], contrast=ui->options[7];
    if (bright < -32 || bright > 32 || contrast < -32 || contrast > 32)
        return cm_fail("options",6,"gamma outside original slider range");
    for (i=3;i<=5;++i)
        if (ui->options[i]<0 || ui->options[i]>100)
            return cm_fail("options",i,"volume outside slider range");
    for (i=0;i<768;i+=3) {
        int rgb[3], mean;
        for (c=0;c<3;++c) rgb[c]=dac_clip((base[i+c]>>2)+bright);
        mean=(rgb[0]+rgb[1]+rgb[2])/3;
        for (c=0;c<3;++c)
            palette[i+c]=(uint8_t)(dac_clip(rgb[c]+(mean<32?-contrast:contrast))*4);
    }
    if (!ui->volumes_applied ||
        memcmp(ui->applied_volumes,ui->options+3,sizeof ui->applied_volumes)) {
        snd_volumes(ui->options[3]*AUDIO_VOLUME_MAX/100,
                    ui->options[4]*AUDIO_VOLUME_MAX/100,
                    (unsigned)ui->options[5]*AUDIO_MIDI_VOLUME_MAX/100);
        memcpy(ui->applied_volumes,ui->options+3,sizeof ui->applied_volumes);
        ui->volumes_applied=1;
    }
    return 0;
}
