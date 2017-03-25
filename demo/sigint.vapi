[CCode(cheader_filename="sigint.h")]
class Sigint
{
    public delegate void Handler();

    public static void add_watch(Handler handler);
    public static void remove_watch();
}
