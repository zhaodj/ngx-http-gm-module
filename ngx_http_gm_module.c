#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct{
    ngx_uint_t width;
    ngx_uint_t height;
}ngx_http_gm_rule_t;

typedef struct{
    ngx_array_t *rules;
    ngx_int_t convert;
}ngx_http_gm_loc_conf_t;

static ngx_int_t ngx_http_gm_handler(ngx_http_request_t *r);

static ngx_int_t ngx_http_gm_init(ngx_conf_t *cf);

static void *ngx_http_gm_create_loc_conf(ngx_conf_t *cf);

static char *ngx_http_gm_rule(ngx_conf_t *cf, ngx_command_t *cmd,void *conf);

static char *ngx_http_gm_convert(ngx_conf_t *cf, ngx_command_t *cmd,void *conf);

static int parse_path(ngx_http_request_t *r,ngx_str_t *path,ngx_str_t *orig,ngx_uint_t *width,ngx_uint_t *height);

static ngx_int_t ngx_http_gm_resize(ngx_http_request_t *r,ngx_http_gm_loc_conf_t *gmlcf,ngx_str_t *orig,ngx_uint_t *width,ngx_uint_t *height);

static ngx_command_t ngx_http_gm_commands[] = {
    {
        ngx_string("gm_allow"),
        NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
        ngx_http_gm_rule,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL
    },
    {
        ngx_string("gm_convert"),
        NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
        ngx_http_gm_convert,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_gm_loc_conf_t, convert),
        NULL },
    ngx_null_command
};


static ngx_http_module_t ngx_http_gm_module_ctx = {
    NULL,                          /* preconfiguration */
    ngx_http_gm_init,           /* postconfiguration */

    NULL,                          /* create main configuration */
    NULL,                          /* init main configuration */

    NULL,                          /* create server configuration */
    NULL,                          /* merge server configuration */

    ngx_http_gm_create_loc_conf, /* create location configuration */
    NULL                            /* merge location configuration */
};


ngx_module_t ngx_http_gm_module = {
    NGX_MODULE_V1,
    &ngx_http_gm_module_ctx,    /* module context */
    ngx_http_gm_commands,       /* module directives */
    NGX_HTTP_MODULE,               /* module type */
    NULL,                          /* init master */
    NULL,                          /* init module */
    NULL,                          /* init process */
    NULL,                          /* init thread */
    NULL,                          /* exit thread */
    NULL,                          /* exit process */
    NULL,                          /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t ngx_http_gm_handler(ngx_http_request_t *r)
{
    u_char                    *last;//, *location
    size_t                     root;
    ngx_str_t                  path;
    ngx_open_file_info_t       of;
    ngx_http_core_loc_conf_t  *clcf;
    ngx_http_gm_loc_conf_t  *gmlcf;

    gmlcf = ngx_http_get_module_loc_conf(r,ngx_http_gm_module);

    if (gmlcf->convert == NGX_CONF_UNSET || gmlcf->convert==0){
        return NGX_DECLINED;
    }
    //只支持get,head
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_DECLINED;
    }

    last = ngx_http_map_uri_to_path(r, &path, &root, 0);
    if (last == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    path.len = last - path.data;
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, log, 0,"http filename: \"%s\"", path.data);

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
    ngx_memzero(&of, sizeof(ngx_open_file_info_t));

    of.read_ahead = clcf->read_ahead;
    of.directio = clcf->directio;
    of.valid = clcf->open_file_cache_valid;
    of.min_uses = clcf->open_file_cache_min_uses;
    of.errors = clcf->open_file_cache_errors;
    of.events = clcf->open_file_cache_events;

    if (ngx_http_set_disable_symlinks(r, clcf, &path, &of) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    //判断请求文件是否存在
    if (ngx_open_cached_file(clcf->open_file_cache, &path, &of, r->pool)!= NGX_OK)
    {
        //解析请求原文件及需要的缩略图尺寸
        ngx_str_t origpath;
        ngx_uint_t width;
        ngx_uint_t height;
        if(parse_path(r,&path,&origpath,&width,&height)==0){
            //验证原文件是否存在
            u_char op[origpath.len+1];
            ngx_cpystrn(op,origpath.data,origpath.len+1);
            if(access((char *)op,R_OK)==0){
                if(gmlcf->rules){
                    //验证尺寸是否允许及生成缩略图
                    return ngx_http_gm_resize(r,gmlcf,&origpath,&width,&height);
                }
            }
        }
    }
    return NGX_DECLINED;
}

static int parse_path(ngx_http_request_t *r,ngx_str_t *path,ngx_str_t *orig,ngx_uint_t *width,ngx_uint_t *height){
    u_char *last=path->data+path->len-1;
    u_char *begin,*end;
    begin=last;
    end=last;
    size_t i=0,j=0;
    while(i<2&&j<path->len){
        if(*last=='.'){
            i++;
            if(i==1){
                end=last;
            }
            if(i==2){
                begin=last;
            }
        }
        last--;
        j++;
    }
    int len=begin-path->data;
    if(len<0){
        return 1;
    }
    orig->data=path->data;
    orig->len=len;
    len=end-begin;
    if(len<0){
        return 1;
    }
    u_char result[len];
    u_char *dest;
    begin++;
    ngx_cpystrn(result,begin,len);
    dest=result;
    int w,h;
    sscanf((char *)dest,"%dx%d",&w,&h);
    *width=(ngx_uint_t)w;
    *height=(ngx_uint_t)h;
    return 0;
}

static ngx_int_t ngx_http_gm_resize(ngx_http_request_t *r,ngx_http_gm_loc_conf_t *gmlcf,ngx_str_t *orig,ngx_uint_t *width,ngx_uint_t *height){
    ngx_uint_t i;
    ngx_http_gm_rule_t *rule;

    rule=gmlcf->rules->elts;
    for(i=0;i<gmlcf->rules->nelts;i++){
        if(*width==rule[i].width&&*height==rule[i].height){
            u_char origpath[orig->len+1];
            ngx_cpystrn(origpath,orig->data,orig->len+1);
            //生成缩略图
            u_char cmd[300];
            u_char *p=ngx_sprintf(cmd,"gm convert %s -resize %ix%i %s.%ix%i.jpg%Z",origpath,*width,*height,origpath,*width,*height);
            if(p){
                char *ccmd=(char *)cmd;
                int err=system(ccmd);
                if(err!=0){
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"generate thumbnail fail");
                }
            }
            return NGX_DECLINED;
        }
    }
    return NGX_DECLINED;
}

static void *ngx_http_gm_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_gm_loc_conf_t* local_conf = NULL;
    local_conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_gm_loc_conf_t));
    if (local_conf == NULL)
    {
        return NULL;
    }
    local_conf->convert=NGX_CONF_UNSET;

    return local_conf;
}

//rule配置解析
static char *ngx_http_gm_rule(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    //ngx_http_core_loc_conf_t *clcf;
    ngx_http_gm_loc_conf_t *gmlcf=conf;
    ngx_str_t *value;
    ngx_http_gm_rule_t *rule;

    value=cf->args->elts;

    if(cf->args->nelts<3){
        return NGX_CONF_ERROR;
    }
    if (gmlcf->rules == NULL) {
        gmlcf->rules = ngx_array_create(cf->pool, 4, sizeof(ngx_http_gm_rule_t));
        if (gmlcf->rules == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    rule = ngx_array_push(gmlcf->rules);
    if (rule == NULL) {
        return NGX_CONF_ERROR;
    }

    rule->width=(ngx_uint_t)ngx_atoi(value[1].data,value[1].len);
    rule->height=(ngx_uint_t)ngx_atoi(value[2].data,value[2].len);

    //clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    //char* rv = ngx_conf_set_str_slot(cf, cmd, conf);


    return NGX_CONF_OK;
}

static char *ngx_http_gm_convert(ngx_conf_t *cf, ngx_command_t *cmd,void *conf)
{
    ngx_http_gm_loc_conf_t* local_conf;

    local_conf = conf;

    char* rv = NULL;

    rv = ngx_conf_set_flag_slot(cf, cmd, conf);

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0, "gm convert:%d", local_conf->convert);
    return rv;
}

static ngx_int_t ngx_http_gm_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_gm_handler;

    return NGX_OK;
}

