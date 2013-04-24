#include "config.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#include <io.h>
#endif

#include "libgimp/gimp.h"
#include "libgimp/stdplugins-intl.h"


#define LOAD_PROC      "file-mpo-load"
#define SAVE_PROC      "file-mpo-save"
#define PLUG_IN_BINARY "file-mpo"
#define PLUG_IN_ROLE   "gimp-file-mpo"

static void              query               (void);
static void              run                 (const gchar      *name,
                                              gint              nparams,
                                              const GimpParam  *param,
                                              gint             *nreturn_vals,
                                              GimpParam       **return_vals);


static gboolean          load_image          (const gchar  *filename,
                                              GError      **error);
static gboolean          split_mpo           (const gchar  *filename);


static gint           num_images = 0; /*Number of images in MPO file*/
static gchar        **image_name;
static FILE          *fp;
static gint32         image_id;

const GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,   /* init_proc  */
  NULL,   /* quit_proc  */
  query,  /* query_proc */
  run,    /* run_proc   */
};

MAIN()

static void
query (void)
{
  static const GimpParamDef load_args[] =
  {
    { GIMP_PDB_INT32,  "run-mode",     "The run mode { RUN-NONINTERACTIVE (1) }" },
    { GIMP_PDB_STRING, "filename",     "The name of the file to load" },
    { GIMP_PDB_STRING, "raw-filename", "The name entered"             }
  };

  static const GimpParamDef load_return_vals[] =
  {
    { GIMP_PDB_IMAGE, "image", "Output image" }
  };

  gimp_install_procedure (LOAD_PROC,
                          "Load MPO files",
                          "No help till now",
                          "Sashi Kumar <ksashikumark93@gmail.com>",
                          "Sashi Kumar <ksashikumark93@gmail.com>",
                          "24th April 2013",
                          N_("3D MPO Data"),
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (load_args),
                          G_N_ELEMENTS (load_return_vals),
                          load_args, load_return_vals);

  gimp_register_load_handler (LOAD_PROC, "mpo", "");


}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[2];
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GError            *error  = NULL;

  INIT_I18N ();

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (strcmp (name, LOAD_PROC) == 0)
    {
      
      if (load_image (param[1].data.d_string, &error))
        {
          *nreturn_vals = 2;
           values[1].type         = GIMP_PDB_IMAGE;
           values[1].data.d_image = image_id;
        }
      else
        {
          status = GIMP_PDB_EXECUTION_ERROR;
        }
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }


  if (status != GIMP_PDB_SUCCESS && error)
    {
      *nreturn_vals = 2;
      values[1].type           = GIMP_PDB_STRING;
      values[1].data.d_string  = error->message;
    }

  values[0].data.d_status = status;
}

static gboolean
load_image (const gchar  *filename,
            GError      **error)
{
  gint32  layer_id;
  gint32  size;
  gint    i;
  GdkPixbuf* pixbuf;
  gboolean status;  

  fp = g_fopen (filename, "rb");
  
  if (!fp)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Could not open '%s' for reading: %s"),
                   gimp_filename_to_utf8 (filename), g_strerror (errno));
      return FALSE;
    }

  if (split_mpo (filename))
    {
      for (i = num_images - 1; i >= 0; i--)
        {

          gchar  *layer_name = g_new0 (gchar, 100); /*Bad Assumption*/
          sprintf (layer_name, "image#%d", i+1);
          pixbuf = gdk_pixbuf_new_from_file(image_name[i], NULL);

          remove (image_name[i]); /*delete the temporary JPEG files*/         
          
          if (pixbuf)
            {
            
              if (i == num_images - 1)
                {
                  image_id = gimp_image_new (gdk_pixbuf_get_width (pixbuf),
                                             gdk_pixbuf_get_height (pixbuf),
                                             GIMP_RGB);
                  gimp_image_set_filename (image_id, filename);
                }

              layer_id = gimp_layer_new_from_pixbuf (image_id, _(layer_name),
                                                     pixbuf,
                                                     100.,
                                                     GIMP_NORMAL_MODE, 0, 0);
              g_object_unref (pixbuf);
    
              gimp_image_insert_layer (image_id, layer_id, -1, -1);
              status = TRUE;
    
           }
          else
            status = FALSE;
          free (layer_name);
        }
    }
  else
    status = FALSE;

  free (image_name);

  return status;

}

static gboolean
split_mpo (const gchar  *filename)
{
  size_t length;  /*total length of file*/
  size_t amount;  /*amount read*/
  gint   i = 0;
  gchar* buffer;
  gchar* fnmbase;
  gchar* ext;
  gchar *temp;

  fnmbase = strdup(filename);

  ext = strstr(fnmbase,".MPO");

  if (ext != NULL) 
      ext[0] = '\0';

  ext = strstr(fnmbase,".mpo");

  if (ext != NULL) 
      ext[0] = '\0';

  /*obtain file size:*/
  fseek(fp, 0, SEEK_END);
  length = ftell(fp);
  rewind(fp);

  /*allocate memory to contain the whole file:*/
  buffer = g_new0 (gchar ,length);

  amount = fread(buffer,1,length,fp);
  if (amount != length) 
      return FALSE;
  fclose(fp);

  /*Now find the individual JPGs*/

  gchar* view = buffer;
  gchar* last = NULL;
  image_name  = g_new (gchar *, 256);

  while (view < buffer+length-4) 
    {
      if (((char) view[0] % 255) == (char) 0xff) 
        {
          if (((char) view[1] % 255) == (char) 0xd8) 
            {
              if (((char) view[2] % 255) == (char) 0xff) 
                {
                  if (((char) view[3] % 255) == (char) 0xe1) 
                    {
                      num_images++;
                      if (last != NULL) 
                        {
                          /*copy out the previous view*/                   
                          image_name[i] = malloc (sizeof(gchar) * 30); /*Bad Assumption*/
                          sprintf(image_name[i], "%s.image#%d.jpg", fnmbase, num_images-1);
                          FILE* w = fopen(image_name[i], "wb");
                          fwrite(last, 1, view-last, w);
                          fclose(w);
                          i++;
                        }
                      last = view;
                      view+=4;
                    } 
                  else 
                    view+=2;
                } 
              else 
                view+=3;
            } 
          else 
            view+=1;
        } 
      else 
        view+=1;
    }

  if (num_images > 1) 
    {
      
      image_name[i] = malloc (sizeof(gchar) * 30);
      sprintf(image_name[i], "%s.image#%d.jpg", fnmbase, num_images);
      FILE* w = fopen(image_name[i], "wb");
      fwrite(last, 1, buffer+length-last, w);
      fclose(w);
    } 

  g_free(buffer);

  return TRUE;

}

