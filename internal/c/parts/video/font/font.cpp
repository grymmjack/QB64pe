//----------------------------------------------------------------------------------------------------------------------
// QB64-PE Font Library
// Powered by FreeType 2.4.12 (https://github.com/vinniefalco/FreeTypeAmalgam)
//----------------------------------------------------------------------------------------------------------------------

//[ ]what if no glyph index could be found and it returns 0??
//[ ]FT_Done_Face
//[ ]Check for memory leaks

#include "font.h"
#include "freetypeamalgam.h"
#include "rounding.h"

FT_Library ft_library;

struct fonts_struct {
    uint8_t in_use;
    uint8_t *ttf_data;
    int32_t default_pixel_height;
    uint8_t bold;
    uint8_t italic;
    uint8_t underline;
    uint8_t monospace;
    int32_t monospace_width;
    uint8_t unicode;
    //---------------------------------
    FT_Face handle;
    int32_t baseline;
    float default_pixel_height_scale;
};
fonts_struct *fonts = (fonts_struct *)malloc(1);
int32_t fonts_last = 0;

struct fonts_render_struct {
    uint8_t *data;
    int32_t w;
    int32_t ox;
};

/// @brief This initializes stb_truetype with the font in memory. The font data is locally copied and is kept alive while in use
/// @param content_original The original font data in memory that is copied
/// @param content_bytes The length of the data in bytes
/// @param default_pixel_height The maximum rendering height of the font
/// @param which_font The font index in a font collection (< 0 means default)
/// @param options 1=bold, 2=italic, 4=underline, 8=IGNORED, 16=monospace, 32=unicode (UTF-32), 64=UTF-8
/// @return A valid font handle (> 0) or 0 on failure
int32_t FontLoad(uint8_t *content_original, int32_t content_bytes, int32_t default_pixel_height, int32_t which_font, int32_t options) {

    static int32_t ft_init_called = 0;
    if (!ft_init_called) {
        ft_init_called = 1;
        if (FT_Init_FreeType(&ft_library))
            exit(5633);
    }

    if (which_font == -1)
        which_font = 0;

    // options: 1=bold, 2=italic, 4=underline, 8=IGNORED, 16=monospace, 32=unicode

    // get new index
    static int32_t i;
    for (i = 1; i <= fonts_last; i++) {
        if (!fonts[i].in_use) {
            goto got_index;
        }
    } // i
    fonts_last++;
    i = fonts_last;
    fonts = (fonts_struct *)realloc(fonts, sizeof(fonts_struct) * (fonts_last + 1));
    fonts[i].in_use = 0;
got_index:

    memset(&fonts[i], 0, sizeof(fonts_struct));

    // duplicate content
    static uint8_t *content;
    content = (uint8_t *)malloc(content_bytes);
    memcpy(content, content_original, content_bytes);
    fonts[i].ttf_data = content;

    if (FT_New_Memory_Face(ft_library, content, content_bytes, which_font, &fonts[i].handle))
        return 0;
    // Note: "Note that you must not deallocate the memory before calling FT_Done_Face."

    if (FT_Set_Pixel_Sizes(fonts[i].handle, 0, default_pixel_height)) {
        FT_Done_Face(fonts[i].handle);
        return 0;
    }
    fonts[i].default_pixel_height = default_pixel_height;

    /*
    static float m_height; m_height=((float)fonts[i].handle->size->metrics.height)/64.0;
    static float m_up; m_up=((float)fonts[i].handle->size->metrics.ascender)/64.0;
    static float m_down; m_down=-(((float)fonts[i].handle->size->metrics.descender)/64.0);
    static float m_char_height; m_char_height=m_up+m_down;
    static float m_h; m_h=default_pixel_height;
    fonts[i].baseline= (m_h/m_height) * ((m_height-m_char_height)/2.0+m_up) ;
    */
    static float m_height;
    m_height = ((float)fonts[i].handle->size->metrics.height) / 64.0;
    static float m_up;
    m_up = ((float)fonts[i].handle->size->metrics.ascender) / 64.0;
    static float m_h;
    m_h = default_pixel_height;
    fonts[i].baseline = qbr((m_up / m_height) * m_h);

    if (options & 16) {
        // get the width of capital W
        static uint32_t cp;
        cp = 87;
        static uint8_t *data1;
        int32_t w1, h1, pre_x, post_x;
        FontRenderTextUTF32(i, &cp, 1, 1, &data1, &w1, &h1, &pre_x, &post_x);
        fonts[i].monospace_width = w1;
        free(data1);
        fonts[i].monospace = 1;
    } // monospace

    // Note: DO NOT ADD NEW CONTENT HERE, ADD IT ABOVE MONOSPACE CHECK

    fonts[i].in_use = 1;
    return i;
}

/// @brief Frees the font and any locally cached data
/// @param fh A valid font handle
void FontFree(int32_t i) {
    FT_Done_Face(fonts[i].handle);
    free(fonts[i].ttf_data);
    fonts[i].in_use = 0;
}

/// @brief Returns the font width
/// @param fh A valid font handle
/// @return The width of the font if the font is monospaced or zero otherwise
int32_t FontWidth(int32_t i) {
    if (fonts[i].monospace)
        return fonts[i].monospace_width;
    return 0;
}

/// @brief Master rendering routine (to be called by all other functions). None of the pointer args can be NULL
/// @param fh A valid font handle
/// @param codepoint A pointer to an array of UTF-32 codepoints that needs to be rendered
/// @param codepoints The number of codepoints in the array
/// @param options 1 = monochrome where black is 0 & white is 255 with nothing in between
/// @param out_data A pointer to a pointer to the output pixel data (alpha values)
/// @param out_x A pointer to the output width of the rendered text in pixels
/// @param out_y A pointer to the output height of the rendered text in pixels
/// @param out_x_pre_increment A pointer to the amount to move the text horizontally before rendering
/// @param out_x_post_increment A pointer to the amount to move the text horizontally after rendering
/// @return success = 1, failure = 0
int32_t FontRenderTextUTF32(int32_t i, uint32_t *codepoint, int32_t codepoints, int32_t options, uint8_t **out_data, int32_t *out_x, int32_t *out_y,
                            int32_t *out_x_pre_increment, int32_t *out_x_post_increment) {
    // Notes:
    // returns: success{1}/failure{0}
    // options: 1=black{0}&white{255}
    // out_x_increment: the ideal amount to move across horizontally after drawing the text
    // out_data: alpha values for each pixel of the font

    if (codepoints <= 0) {
        *out_data = NULL;
        *out_x = 0;
        *out_y = fonts[i].default_pixel_height;
        *out_x_pre_increment = 0;
        *out_x_post_increment = 0;
        if (codepoints < 0)
            return 0;
        return 1;
    }

    static int32_t monochrome;
    monochrome = options & 1;

    static int32_t codepoint_w;
    static int32_t codepoint_i, codepoint_ox;
    static uint32_t codepoint_value;
    static fonts_render_struct *render;
    static int32_t value;
    static uint8_t *data1, *data2;
    static int w1, h1, ox, oy; // Note: Must be 'int' type
    static int32_t w2, h2, ox2, oy2;
    static int32_t x1, y1;
    static int32_t x2, y2;

    if (codepoints > 1) {
        render = (fonts_render_struct *)malloc(sizeof(fonts_render_struct) * codepoints);
    }

    codepoint_w = 0;
    codepoint_ox = 0;
    for (codepoint_i = 0; codepoint_i < codepoints; codepoint_i++) {
        codepoint_value = codepoint[codepoint_i];

        static int32_t glyph_index;
        glyph_index = FT_Get_Char_Index(fonts[i].handle, codepoint_value);
        if (!glyph_index) {
            // failed!
        }
        if (FT_Load_Glyph(fonts[i].handle, glyph_index, FT_LOAD_DEFAULT)) {
            // failed!
        }

        if (monochrome) {

            if (FT_Render_Glyph(fonts[i].handle->glyph, FT_RENDER_MODE_MONO)) {
                // failed!
            }

        } else {

            if (FT_Render_Glyph(fonts[i].handle->glyph, FT_RENDER_MODE_NORMAL)) {
                // failed!
            }
        }

        static int32_t pitch1;
        pitch1 = fonts[i].handle->glyph->bitmap.pitch;

        ox = fonts[i].handle->glyph->bitmap_left;
        oy = 0;
        h1 = fonts[i].handle->glyph->bitmap.rows;
        w1 = fonts[i].handle->glyph->bitmap.width;
        data1 = (uint8_t *)fonts[i].handle->glyph->bitmap.buffer;

        h2 = fonts[i].default_pixel_height;

        w2 = fonts[i].handle->glyph->advance.x / 64; // default width
        if (w2 < w1)
            w2 = w1;
        ox2 = 0;
        if (ox > 0) {
            if ((w1 + ox) > w2)
                w2 = w1 + ox;
            ox2 = ox;
        }
        if (ox < 0) { // compensate for loss of width from left shift
            w2 = w2 + (-ox);
        }

        // Monospace resize as necessary
        if (fonts[i].monospace) {
            if (w2 != fonts[i].monospace_width) {
                w2 = fonts[i].monospace_width;
                ox = 0;                // no repositioning possible
                ox2 = w2 / 2 - w1 / 2; // align to centre
            }
        }

        data2 = (uint8_t *)calloc(w2 * h2, 1);

        oy2 = fonts[i].baseline - fonts[i].handle->glyph->bitmap_top;

        for (y1 = 0; y1 < h1; y1++) {
            y2 = y1 + oy2;
            if ((y2 >= 0) && (y2 < h2)) {
                for (x1 = 0; x1 < w1; x1++) {
                    x2 = x1 + ox2;
                    if ((x2 >= 0) && (x2 < w2)) {

                        if (monochrome) {
                            data2[x2 + y2 * w2] = ((data1[y1 * pitch1 + x1 / 8] >> (7 - (x1 & 7))) & 1) * 255; // 1-bit
                        } else {
                            data2[x2 + y2 * w2] = data1[x1 + y1 * pitch1]; // 8-bit
                        }
                    }
                } // x1
            }
        } // y1

        // single character render?
        if (codepoints == 1) {
            *out_data = data2;
            *out_x = w2;
            *out_y = h2;
            if (ox < 0)
                *out_x_pre_increment = ox;
            else
                *out_x_pre_increment = 0;
            *out_x_post_increment = 0;
            return 1;
        }

        if (codepoint_i == 0) {
            if (ox < 0) {
                *out_x_pre_increment = ox;
            } else {
                *out_x_pre_increment = 0;
            }
        } else {
            if (ox < 0) { // regress codepoint_ox?
                if ((codepoint_ox + ox) >= 0) {
                    codepoint_ox += ox;
                } else {
                    codepoint_ox = 0;
                }
            }
        }
        render[codepoint_i].data = data2;
        render[codepoint_i].w = w2;
        render[codepoint_i].ox = codepoint_ox;
        codepoint_ox += w2;
        if (codepoint_ox > codepoint_w)
            codepoint_w = codepoint_ox;

    } // codepointi loop

    // join&'blend' multiple codepoints
    w2 = codepoint_w;
    h2 = fonts[i].default_pixel_height;
    data2 = (uint8_t *)calloc(w2 * h2, 1);
    for (codepoint_i = 0; codepoint_i < codepoints; codepoint_i++) {

        data1 = render[codepoint_i].data;
        w1 = render[codepoint_i].w;
        h1 = h2;

        ox2 = render[codepoint_i].ox;

        for (y1 = 0; y1 < h1; y1++) {
            y2 = y1;
            for (x1 = 0; x1 < w1; x1++) {
                x2 = x1 + ox2;
                value = data1[x1 + y1 * w1];
                if (value > data2[x2 + y2 * w2])
                    data2[x2 + y2 * w2] = value;
            } // x1
        }     // y1
        free(data1);

    } // codepoint_i

    *out_data = data2;
    *out_x = w2;
    *out_y = h2;
    // Note: '*out_x_pre_increment' is set above
    *out_x_post_increment = 0;
    if (codepoints > 1)
        free(render);
    return 1;
}

/// @brief This will call FontRenderTextUTF32() after converting the ASCII codepoint array to UTF-32. None of the pointer args can be NULL
/// @param fh A valid font handle
/// @param codepoint A pointer to an array of ASCII codepoints that needs to be rendered
/// @param codepoints The number of codepoints in the array
/// @param options 1 = monochrome where black is 0 & white is 255 with nothing in between
/// @param out_data A pointer to a pointer to the output pixel data (alpha values)
/// @param out_x A pointer to the output width of the rendered text in pixels
/// @param out_y A pointer to the output height of the rendered text in pixels
/// @param out_x_pre_increment A pointer to the amount to move the text horizontally before rendering
/// @param out_x_post_increment A pointer to the amount to move the text horizontally after rendering
/// @return success = 1, failure = 0
int32_t FontRenderTextASCII(int32_t i, uint8_t *codepoint, int32_t codepoints, int32_t options, uint8_t **out_data, int32_t *out_x, int32_t *out_y,
                            int32_t *out_x_pre_increment, int32_t *out_x_post_increment) {
    static uint32_t *utf32_codepoint;
    static int32_t retval;
    if (codepoints >= 1) {
        utf32_codepoint = (uint32_t *)malloc(codepoints * 4);
        static int32_t x;
        for (x = 0; x < codepoints; x++) {
            utf32_codepoint[x] = codepage437_to_unicode16[codepoint[x]];
        }
    }
    retval = FontRenderTextUTF32(i, utf32_codepoint, codepoints, options, out_data, out_x, out_y, out_x_pre_increment, out_x_post_increment);
    if (codepoints > 0)
        free(utf32_codepoint);
    return retval;
}

/// @brief Returns the length of an ASCII codepoint string in pixels
/// @param fh A valid font
/// @param codepoint The ASCII codepoint array
/// @param codepoints The number of codepoints
/// @return Length in pixels
int32_t FontPrintWidthASCII(int32_t fh, uint8_t *codepoint, int32_t codepoints) { return 0; }
