nginx http graphicsmagick module
===

nginx handler模块，访问图片文件时，根据配置自动生成缩略图

## 安装
- 安装GraphicsMagick
- nginx源代码目录下执行

	```
	./configure --prefix=/usr/local/nginx-1.4.1 --add-module=/home/zhaodj/source/ngx-http-gm-module
	```
	--add-module参数为此项目路径
	
	```
	sudo make & sudo make install
	```

## 配置
- 在nginx配置文件中对应的location下添加如下配置，数字分别对应允许的图片缩略图宽和高

	```
	gm_allow 400 300;
	gm_allow 800 600;
	```
	
- 缩略图命名规则：原图```http://{host}/img/name.jpg```，缩略图访问时使用```http://{host}/img/name.jpg.400x300.jpg```

- nginx工作进程以nobody用户运行，需要将图片目录设置为nobody可写
	

## TODO
- gm命令path配置
- 自动识别文件扩展名

## 参考
- [Nginx开发从入门到精通](http://tengine.taobao.org/book/index.html)

