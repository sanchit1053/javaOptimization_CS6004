
public class T4 {
    public static int factorial2(int i){
        if(i==0){
            return 1;
        }
        return i*factorial(i-1);
    }
    public static void main(String[] args) {
        int i = 5;
        System.out.println(factorial(i));
    }

    private static int factorial(int i) {
        if(i==0){
            return 1;
        }
        return i*factorial2(i-1);
    }
}
