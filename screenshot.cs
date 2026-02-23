using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Windows.Forms;

class Program
{
    static void Main(string[] args)
    {
        try
        {
            Rectangle bounds = Screen.PrimaryScreen.Bounds;
            using (Bitmap bitmap = new Bitmap(bounds.Width, bounds.Height))
            {
                using (Graphics g = Graphics.FromImage(bitmap))
                {
                    g.CopyFromScreen(Point.Empty, Point.Empty, bounds.Size);
                }
                bitmap.Save(@"c:\DynamicWallpaperSource\screenshot.png", ImageFormat.Png);
            }
            Console.WriteLine("Screenshot saved.");
        }
        catch (Exception ex)
        {
            Console.WriteLine("Error: " + ex.Message);
        }
    }
}
