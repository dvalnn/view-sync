package sdis.transport;

public class SyncSocket extends Thread{
    private DatagramSocket socket;
    private boolean blocking;
    private boolean fallible;

    public SyncSocket create(int port, boolean blocking, boolean fallible){
        socket = new DatagramSocket(port);
        blocking = blocking;
        fallible = fallible;
    }
}
