
public class T3 {
    public static void main(String[] args) {
        int i = 5;
        System.out.println(factorial(i));
    }

    private static int factorial(int i) {
        if(i==0){
            return 1;
        }
        return i*factorial(i-1);
    }
}
