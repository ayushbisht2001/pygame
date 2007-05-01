/*
    pygame - Python Game Library
    Copyright (C) 2006, 2007 Rene Dudfield, Marcus von Appen

    Originally written and put in the public domain by Sam Lantinga.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <Windows.h>

static HWND SDL_Window;
#define MAX_CHUNK_SIZE INT_MAX

static UINT _format_MIME_PLAIN;

/**
 * \brief Converts the passed type into a system specific clipboard type
 *        to use for the clipboard.
 *
 * \param type The type to convert.
 * \return A system specific type.
 */
static UINT
_convert_format (char *type)
{
    return RegisterClipboardFormat (type);
}

/**
 * \brief Gets a system specific clipboard format type for a certain type.
 *
 * \param type The name of the format to get the mapped format type for.
 * \return The format type or -1 if no such type was found.
 */
static UINT
_convert_internal_type (char *type)
{
    if (strcmp (type, PYGAME_SCRAP_TEXT) == 0)
        return CF_TEXT;
    if (strcmp (type, "text/plain;charset=utf-8") == 0)
        return CF_UNICODETEXT;
    if (strcmp (type, "image/tiff") == 0)
        return CF_TIFF;
    if (strcmp (type, PYGAME_SCRAP_BMP) == 0)
        return CF_BITMAP;
    if (strcmp (type, "audio/wav") == 0)
        return CF_WAVE;
    return -1;
}

/**
 * \brief Looks up the name for the specific clipboard format type.
 *
 * \param format The format to get the name for.
 * \param buf The buffer to copy the name into.
 * \param size The size of the buffer.
 * \return The length of the format name.
 */
static int
_lookup_clipboard_format (UINT format, char *buf, int size)
{
    int len;
    char *cpy;

    memset (buf, 0, size);
    switch (format)
    {
    case CF_TEXT:
        len = strlen (PYGAME_SCRAP_TEXT);
        cpy = PYGAME_SCRAP_TEXT;
        break;
    case CF_UNICODETEXT:
        len = 24;
        cpy = "text/plain;charset=utf-8";
        break;
    case CF_TIFF:
        len = 10;
        cpy = "image/tiff";
        break;
    case CF_BITMAP:
        len = strlen (PYGAME_SCRAP_BMP);
        cpy = PYGAME_SCRAP_BMP;
        break;
    case CF_WAVE:
        len = 9;
        cpy = "audio/wav";
        break;
    default:
        len = GetClipboardFormatName (format, buf, size);
        return len;
    }
    if (len != 0 )
        memcpy (buf, cpy, len);
    return len;
}

int
pygame_scrap_init (void)
{
    SDL_SysWMinfo info;
    int retval = 0;

    /* Grab the window manager specific information */
    SDL_SetError ("SDL is not running on known window manager");

    SDL_VERSION (&info.version);
    if (SDL_GetWMInfo (&info))
    {
        /* Save the information for later use */
        SDL_Window = info.window;
        retval = 1;
    }
    if (retval)
        _scrapinitialized = 1;
    
    _format_MIME_PLAIN = RegisterClipboardFormat ("text/plain");
    return retval;
}

int
pygame_scrap_lost (void)
{
    if (!pygame_scrap_initialized ())
    {
        PyErr_SetString (PyExc_SDLError, "scrap system not initialized.");
        return 0;
    }
    return (GetClipboardOwner () != SDL_Window);
}

int
pygame_scrap_put (char *type, int srclen, char *src)
{
    UINT format;
    int nulledlen = srclen + 1;
    HANDLE hMem;

    if (!pygame_scrap_initialized ())
    {
        PyErr_SetString (PyExc_SDLError, "scrap system not initialized.");
        return 0;
    }

    format = _convert_format (type);

    if (!OpenClipboard (SDL_Window))
        return 0; /* Could not open the clipboard. */
    
    hMem = GlobalAlloc ((GMEM_MOVEABLE | GMEM_DDESHARE), nulledlen);
    if (hMem)
    {
        char *dst = GlobalLock (hMem);

        memset (dst, 0, nulledlen);
        memcpy (dst, src, srclen);

        GlobalUnlock (hMem);
        EmptyClipboard ();
        SetClipboardData (format, hMem);
        
        if (format == _format_MIME_PLAIN) 
        {
            /* Setting SCRAP_TEXT, also set CF_TEXT. */
            SetClipboardData (CF_TEXT, hMem);
        }
        CloseClipboard ();
    }
    else
    {
        /* Could not access the clipboard, raise an error. */
        CloseClipboard ();
        return 0;
    }

    return 1;
}

char*
pygame_scrap_get (char *type, unsigned long *count)
{
    UINT format = _convert_format (type);
    char *retval = NULL;

    if (!pygame_scrap_initialized ())
    {
        PyErr_SetString (PyExc_SDLError, "scrap system not initialized.");
        return 0;
    }

    if (!pygame_scrap_lost ())
        return PyString_AsString (PyDict_GetItemString (_clipdata, type));

    if (!OpenClipboard (SDL_Window))
        return NULL;

    if (!IsClipboardFormatAvailable (format))
    {
        /* The format was not found - was it a mapped type? */
        format = _convert_internal_type (type);
        if (format == -1)
            return NULL;
    }

    if (IsClipboardFormatAvailable (format))
    {
        HANDLE hMem;
        char *src;
        
        hMem = GetClipboardData (format);
        if (hMem)
        {
            *count = 0;
            
            /* TODO: Is there any mechanism to detect the amount of bytes
             * in the HANDLE? strlen() won't work as supposed, if the
             * sequence contains NUL bytes. Can this even happen in the 
             * Win32 clipboard or is NUL the usual delimiter?
             */
            src = GlobalLock (hMem);
            *count = strlen (src) + 1;
            
            retval = malloc (*count);
            if (retval)
            {
                memset (retval, 0, *count);
                memcpy (retval, src, *count);
            }
            GlobalUnlock (hMem);
        }
        CloseClipboard ();
    }
    
    return retval;
}

char**
pygame_scrap_get_types (void)
{
    UINT format = 0;
    char **types = NULL;
    char **tmptypes;
    int i = 0;
    int count = -1;
    int len;
    char tmp[100] = { '\0' };
    int size = 0;

    if (!OpenClipboard (SDL_Window))
        return NULL;
    size = CountClipboardFormats ();
    if (size == 0)
        return NULL; /* No clipboard data. */

    //types = malloc (sizeof (char *) * (size + 1));
    for (i = 0; i < size; i++)
    {
        format = EnumClipboardFormats (format);
        if (format == 0)
        {
            /* Something wicked happened. */
            while (i > 0)
                free (types[i]);
            free (types);
            return NULL;
            break;
        }

        /* No predefined name, get the (truncated) name. */
        len = _lookup_clipboard_format (format, tmp, 100);
        if (len == 0)
            continue;
        count++;

        tmptypes = realloc (types, sizeof (char *) * (count + 1));
        if (!tmptypes)
        {
            while (count > 0)
            {
                free (types[count]);
                count--;
            }
            free (types);
            return NULL;
        }
        types = tmptypes;
        types[count] = malloc (sizeof (char) * (len + 1));
        if (!types[count])
        {
            while (count > 0)
            {
                free (types[count]);
                count--;
            }
            free (types);
            return NULL;
        }

        memset (types[count], 0, len + 1);
        memcpy (types[count], tmp, len);
    }
    tmptypes = realloc (types, sizeof (char *) * (count + 1));
    if (!tmptypes)
    {
        while (count > 0)
        {
            free (types[count]);
            count--;
        }
        free (types);
        return NULL;
    }
    types = tmptypes;
    types[count] = NULL;
    return types;
}

int
pygame_scrap_contains (char *type)
{
    return IsClipboardFormatAvailable (_convert_format(type));
}