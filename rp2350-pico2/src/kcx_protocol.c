/*
 * KCX_BT_EMITTER v1.7 UART protocol, including observed unsolicited lines:
 *   MacAdd:6e73594c1838,Name:WH-1000XM3
 *   CONNECT=>MacAdd:6e73594c1838,Name:WH-1000XM3
 *   CON ONE, POWER ON, SCAN...., OK+STATUS:1
 *
 * Invalid/empty scan records are ignored; only a real module error response
 * raises KCX_EVENT_ERROR. Connected peer information survives list refresh.
 */
#include "kcx_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void kcx_state_init(kcx_state_t *s)
{
    if(!s)return;
    memset(s,0,sizeof(*s));
    s->pending_name_index=-1;
}
void kcx_scan_clear(kcx_state_t *s)
{
    if(!s)return;
    memset(s->devices,0,sizeof(s->devices));
    s->device_count=0;
    s->pending_name_index=-1;
    s->scanning=true;
    s->scan_finished=false;
}
bool kcx_normalize_mac(const char *input,char result[13])
{
    if(!input||!result)return false;
    const char *p=input;
    while(*p==' '||*p=='\t')++p;
    if(p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;
    char normalized[13];
    size_t count=0;
    for(;*p;++p){
        if(*p==':'||*p=='-'||*p==' ')continue;
        if(!isxdigit((unsigned char)*p)||count==12)return false;
        normalized[count++]=(char)tolower((unsigned char)*p);
    }
    if(count!=12)return false;
    normalized[12]=0;
    memcpy(result,normalized,sizeof(normalized));
    return true;
}
static void copy_clean(char *dest,size_t capacity,const char *src)
{
    if(!dest||!capacity)return;
    if(!src)src="";
    while(*src==' '||*src=='\t'||*src==':')++src;
    size_t size=0;
    while(*src&&*src!='\r'&&*src!='\n'&&size+1<capacity)
        dest[size++]=*src++;
    while(size&&(dest[size-1]==' '||dest[size-1]=='\t'))--size;
    dest[size]=0;
}
static unsigned track_device(kcx_state_t *s,const char *payload,bool is_connect)
{
    /* KCX v1.7 sometimes combines MAC and name in the SAME serial line. */
    const char *inline_name=strstr(payload,",Name:");
    size_t mac_chars=inline_name?(size_t)(inline_name-payload):strcspn(payload,", \t\r\n");
    if(mac_chars==0||mac_chars>32)return KCX_EVENT_NONE;
    char mac_text[33];
    memcpy(mac_text,payload,mac_chars);
    mac_text[mac_chars]=0;
    char mac[13];
    if(!kcx_normalize_mac(mac_text,mac))return KCX_EVENT_NONE;

    size_t idx=0;
    while(idx<s->device_count&&strcmp(s->devices[idx].mac,mac)!=0)++idx;
    if(idx==s->device_count){
        if(idx==KCX_MAX_DEVICES)return KCX_EVENT_NONE;
        snprintf(s->devices[idx].mac,sizeof(s->devices[idx].mac),"%s",mac);
        snprintf(s->devices[idx].name,sizeof(s->devices[idx].name),"Unknown device");
        ++s->device_count;
    }
    if(inline_name){
        char name[KCX_DEVICE_NAME_LENGTH];
        copy_clean(name,sizeof(name),inline_name+6);
        if(name[0])snprintf(s->devices[idx].name,sizeof(s->devices[idx].name),"%s",name);
        s->pending_name_index=-1;
    }else{
        s->pending_name_index=(int)idx;
    }
    s->ready=true;
    unsigned event=KCX_EVENT_DEVICE;
    if(is_connect){
        bool changed=!s->status_known||!s->connected||
                     strcmp(s->connected_mac,mac)!=0;
        s->status_known=true;
        s->connected=true;
        s->scanning=false;
        snprintf(s->connected_mac,sizeof(s->connected_mac),"%s",mac);
        snprintf(s->connected_name,sizeof(s->connected_name),"%s",s->devices[idx].name);
        snprintf(s->last_mac,sizeof(s->last_mac),"%s",mac);
        if(changed)event|=KCX_EVENT_LINK;
    }
    return event;
}
unsigned kcx_parse_line(kcx_state_t *s,const char *line)
{
    if(!s||!line)return KCX_EVENT_NONE;
    while(*line==' '||*line=='\t')++line;
    if(!*line)return KCX_EVENT_NONE;

    if(strncmp(line,"CONNECT=>MacAdd:",16)==0)
        return track_device(s,line+16,true);
    if(strncmp(line,"MacAdd:",7)==0)
        return track_device(s,line+7,false);
    if(strncmp(line,"Name:",5)==0&&s->pending_name_index>=0&&
       (size_t)s->pending_name_index<s->device_count){
        kcx_device_t *d=&s->devices[s->pending_name_index];
        copy_clean(d->name,sizeof(d->name),line+5);
        if(s->connected&&strcmp(s->connected_mac,d->mac)==0)
            snprintf(s->connected_name,sizeof(s->connected_name),"%s",d->name);
        s->pending_name_index=-1;
        return KCX_EVENT_DEVICE;
    }
    /* Last_Add is the last actually connected peer, not VM_MacAddN. */
    const char *last=strstr(line,"Last_Add=");
    if(last && (last==line || last[-1]==' ' || last[-1]==',')){
        last+=9;
        size_t length=strcspn(last,", \t\r\n");
        if(length>0 && length<32){
            char source[32],normalized[13];
            memcpy(source,last,length);source[length]=0;
            if(kcx_normalize_mac(source,normalized) &&
               strcmp(normalized,"000000000000")!=0 &&
               strcmp(normalized,"ffffffffffff")!=0){
                s->ready=true;
                if(strcmp(s->last_mac,normalized)!=0){
                    memcpy(s->last_mac,normalized,sizeof(s->last_mac));
                    return KCX_EVENT_SAVED;
                }
                return KCX_EVENT_NONE;
            }
        }
    }
    if(strncmp(line,"SCAN",4)==0||strncmp(line,"New Devices:",12)==0){
        s->scanning=true;
        return KCX_EVENT_SCAN;
    }
    if(strncmp(line,"ALL Devices",11)==0){
        s->scanning=false;
        s->scan_finished=true;
        s->pending_name_index=-1;
        return KCX_EVENT_SCAN;
    }
    if(strncmp(line,"OK+STATUS:",10)==0&&(line[10]=='0'||line[10]=='1')){
        bool linked=line[10]=='1';
        bool changed=!s->status_known||linked!=s->connected;
        s->ready=true;
        s->status_known=true;
        s->connected=linked;
        if(!linked){
            s->connected_mac[0]=0;
            s->connected_name[0]=0;
        }else{
            s->scanning=false;
        }
        return changed?KCX_EVENT_LINK:KCX_EVENT_NONE;
    }
    if(strncmp(line,"OK+BT_EMITTER",13)==0||
       strncmp(line,"OK+BT_RECEIVER",14)==0){
        s->ready=true;
        s->mode_known=true;
        s->transmitter=strncmp(line,"OK+BT_EMITTER",13)==0;
        return KCX_EVENT_MODE;
    }
    if(strncmp(line,"OK+VERS",7)==0){
        const char *separator=strchr(line,':');
        copy_clean(s->version,sizeof(s->version),separator?separator+1:line+7);
        s->ready=true;
        return KCX_EVENT_VERSION;
    }
    if(strncmp(line,"Delete_Vmlink",13)==0){
        s->ready=true;
        s->last_mac[0]=0;
        return KCX_EVENT_ERASE_ACK;
    }
    if(strncmp(line,"OK+ADDLINKADD",13)==0){
        s->ready=true;
        return KCX_EVENT_ADD_ACK;
    }
    if(strncmp(line,"CMD ERR!",8)==0||strstr(line,"Not in emitter mode!")||
       strstr(line,"Addr More than 10")||strstr(line,"Name More than 10"))
        return KCX_EVENT_ERROR;
    if(strncmp(line,"OK+",3)==0){
        s->ready=true;
        return KCX_EVENT_READY;
    }
    /* POWER ON / CON ONE / partial records are not command errors. */
    return KCX_EVENT_NONE;
}
