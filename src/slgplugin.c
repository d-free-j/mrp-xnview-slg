/*
 * SLG Plugin for XnView, defrigerator@mail.ru
 */

#include <stdio.h>
#include <windows.h>

#include <zlib.h>

BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) 
{
	switch (ul_reason_for_call) 
	{
	case DLL_PROCESS_ATTACH :
	case DLL_PROCESS_DETACH :
	case DLL_THREAD_ATTACH  :
	case DLL_THREAD_DETACH  :
		break;
  }
	return TRUE;
}

#define API __stdcall

#define GFP_RGB	0
#define GFP_BGR	1

#define GFP_READ	0x0001
#define GFP_WRITE 0x0002

#define CHUNK 1024
#define windowBits 15
#define ENABLE_ZLIB_GZIP 32

typedef struct {
		unsigned char red[256]; 
		unsigned char green[256]; 
		unsigned char blue[256]; 
	} GFP_COLORMAP; 

BOOL API gfpGetPluginInfo(DWORD version, LPSTR label, INT label_max_size, 
    LPSTR extension, INT extension_max_size, INT * support)
{
	if (version != 0x0002)
		return FALSE; 

	strncpy(label, "SLG Image (defrigerator@mail.ru)", label_max_size); 
	strcpy(extension, "slg"); 

	*support = GFP_READ; 

	return TRUE; 
}

typedef struct {
		FILE * fp; 
		INT width;
		INT height;
		INT mode; // 0 - err, 1 - normal compress, 2 - normal raw,
		          // 3 - without header compress, 4 - without header raw
        UCHAR* bitmap; // raw bitmap
	} SLDDATA; 

typedef struct {
        USHORT width;
        USHORT height;
        UINT datasize;
        UINT formatver;
	} SLGRAW; 

typedef struct {
        USHORT width;
        USHORT height;
        UINT datasize;
	} SLGCOMP; 


USHORT swap_ushort(USHORT val) 
{
    return (val << 8) | (val >> 8 );
}

UINT swap_uint(UINT val)
{
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF); 
    return (val << 16) | (val >> 16);
}

void fillbitmap(UCHAR* bitmap, UCHAR* src, USHORT width, USHORT height)
{
    UINT i, j;
    UCHAR val1, val2, valg;
    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            val2 = src[(j * width + i) * 2];
            val1 = src[(j * width + i) * 2 + 1];
            valg = (val1 & 0x07) << 5 | (val2 & 0xE0) >> 3;
            bitmap[(j * width + i) * 4] = (val1 & 0xF8) | val1 >> 5;
            bitmap[(j * width + i) * 4 + 1] = valg | valg >> 6;
            bitmap[(j * width + i) * 4 + 2] = (val2 & 0x1F) << 3 | (val2 & 0x18) >> 2;
            bitmap[(j * width + i) * 4 + 3] = 0xFF;       
        }
    }
}

void fillalpha(UCHAR* bitmap, UCHAR* src, UINT srclen, USHORT width, USHORT height)
{
    UINT apos = 3, spos = 0;
    UCHAR tp, val;
    for (spos = 0; spos < srclen; spos++) {
        tp = src[spos] & 0xC0;
        val = src[spos] & 0x3F;
        switch (tp) {
            case 0x40: // transparent
                while (val > 0) {
                    bitmap[apos] = 0;
                    apos += 4;
                    val--;
                };
                break;
            case 0x80: // semi
                val = val << 3;
                bitmap[apos] = val | val >> 5;            
                apos += 4;
                break;
            case 0xC0: // opaque
                while (val > 0) {
                    bitmap[apos] = 0xFF;
                    apos += 4;
                    val--;
                };
                break;
            default:
                break;
        }
    
    }

}


void * API gfpLoadPictureInit(LPCSTR filename)
{
	SLDDATA * data;

	data = calloc(1, sizeof(SLDDATA)); 

	data->fp = fopen(filename, "rb");
	if (data->fp == NULL)
	{
		free(data); 
		data = NULL; 
	}
    // read magic
    unsigned int magic;
    UINT mlen = fread(&magic, sizeof(UINT), 1, data->fp);
    data->mode = 0; // err
    if (mlen != 1) {
		free(data); 
		data = NULL; 
    } else if (magic == 0x30676c73) {
        SLGRAW header;
        UINT hlen = fread(&header, sizeof(SLGRAW), 1, data->fp);
        if (hlen == 1) {
		    if (swap_uint(header.formatver) == 6) {
		        data->width = swap_ushort(header.width);
		        data->height = swap_ushort(header.height);
                data->mode = 2; // normal raw
                data->bitmap = calloc(data->width * data->height * 4, sizeof(UCHAR));
                // read bitmap
                UCHAR* buff = calloc(data->width * data->height * 2, sizeof(UCHAR));
                fread(buff, sizeof(UCHAR), data->width * data->height * 2, data->fp);
                fillbitmap(data->bitmap, buff, data->width, data->height);
                free(buff);
                // read alpha
                INT alphsize = swap_uint(header.datasize) - data->width * data->height * 2;
                if (alphsize > 0) {
                    UCHAR* alph = calloc(alphsize, sizeof(UCHAR));
                    fread(alph, sizeof(UCHAR), alphsize, data->fp);
                    fillalpha(data->bitmap, alph, alphsize, data->width, data->height);
                    free(alph);
                }
                return data;
            }
        }    
	    free(data);
	    data = NULL;
    } else if (magic == 0xbbef0000) {
        SLGCOMP header;
        UINT hlen = fread(&header, sizeof(SLGCOMP), 1, data->fp);
        if (hlen == 1) {
            UCHAR* gz = calloc(header.datasize, sizeof(UCHAR));
            UCHAR* unc = NULL;
            UINT uncsize = 0;
            // start ZLIB
            UCHAR out[CHUNK];
            z_stream strm;
            memset(&strm, 0, sizeof(z_stream));
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.next_in = gz;
            strm.avail_in = header.datasize;
            strm.next_out = out;
            strm.avail_out = CHUNK;
            
            UINT gzsize = fread(gz, sizeof(UCHAR), header.datasize, data->fp);
            INT zstatus = inflateInit2(&strm, windowBits | ENABLE_ZLIB_GZIP);      
            if ((gzsize != header.datasize) || (zstatus < 0)) {
                free(gz);
                free(unc); // no data
                free(data);
                data = NULL;
                return data;
            }
            // receive uncompressed chunks
            strm.avail_in = gzsize;
            do {
                strm.avail_out = CHUNK;
                strm.next_out = out;
                //strm.avail_in = header.datasize;
                //strm.next_in = gz;
                zstatus = inflate(&strm, Z_NO_FLUSH);
                if (zstatus < 0) {
                    free(unc);
                    free(data); 
                    data = NULL;
                    return NULL;                            
                }
                // consume data
                // realoc
                UINT ustart = uncsize;
                uncsize += (CHUNK - strm.avail_out);
                unc = realloc(unc, uncsize * sizeof(UCHAR));
                memcpy(&unc[ustart], out, CHUNK - strm.avail_out);
            } while (strm.avail_out == 0);
            
            inflateEnd (&strm);
            free(gz);

            // parse data
            data->width = header.width;
            data->height = header.height;
            data->mode = 1; // normal compess
            data->bitmap = calloc(data->width * data->height * 4, sizeof(UCHAR));
            if (uncsize > 0) {
                fillbitmap(data->bitmap, unc, data->width, data->height);
                INT alphsize = uncsize - (data->width * data->height * 2);
                if (alphsize > 0) {
                    fillalpha(data->bitmap, &unc[data->width * data->height * 2], 
                        alphsize, data->width, data->height);
                }
            }
            free(unc);
                  
            return data;
        }
	    free(data); 
	    data = NULL;    
    } else {
		free(data); 
		data = NULL; 
    }

	return data; 
}

BOOL API gfpLoadPictureGetInfo(void * ptr, INT * pictype, INT * width, INT * height, INT * dpi, INT * bits_per_pixel, INT * bytes_per_line, BOOL * has_colormap, LPSTR label, INT label_max_size)
{
	SLDDATA * data = (SLDDATA *)ptr; 

    *width = data->width;
    *height = data->height;
    *bits_per_pixel = 32;

	*pictype = GFP_RGB; 
	*dpi = 72; 
	*bytes_per_line = *bits_per_pixel/8 * *width * 4; 
	
	*has_colormap = FALSE; 
	
	if (data->mode == 1) {
	    strncpy(label, "SLG compressed image", label_max_size); 
    } else if (data->mode == 2) {
	    strncpy(label, "SLG image", label_max_size); 
    } else {
	    strncpy(label, "Not a SLG image", label_max_size); 
    }

	return TRUE; 
}

BOOL API gfpLoadPictureGetLine( void * ptr, INT line, unsigned char * buffer )
{
	SLDDATA * data = (SLDDATA *)ptr; 
	memcpy(buffer, &data->bitmap[line * data->width * 4], data->width * 4);
	return TRUE; 
}

BOOL API gfpLoadPictureGetColormap( void * ptr, GFP_COLORMAP * cmap )
{
	return FALSE; 
}

void API gfpLoadPictureExit( void * ptr )
{
	SLDDATA * data = (SLDDATA *)ptr; 
	free(data->bitmap); 
	fclose(data->fp); 
	free(data); 
}

