#include "BridgeUtils.h"

/**
 * Helper function used to convert decimal to hexadecimal
 */ 
std::string decToHexa(int n)
{
    // ans string to store hexadecimal number
    std::string ans = "";
   
    while (n != 0) {
        // remainder variable to store remainder
        int rem = 0;
         
        // ch variable to store each character
        char ch;
        // storing remainder in rem variable.
        rem = n % 16;
 
        // check if temp < 10
        if (rem < 10) {
            ch = rem + 48;
        }
        else {
            ch = rem + 55;
        }
         
        // updating the ans string with the character variable
        ans += ch;
        n = n / 16;
    }
     
    // reversing the ans string to get the final result
    int i = 0, j = ans.size() - 1;
    while(i <= j)
    {
      std::swap(ans[i], ans[j]);
      i++;
      j--;
    }
    return ans;
}

/**
 * Helper function used to create a string representation of a esp_ip6_addr_t
 */
std::string Ip6ToStr(esp_ip6_addr_t &ip6addr)
{
    auto block1 = (uint16_t)((esp_netif_htonl(ip6addr.addr[0]) >> 16) & 0xffff);
    auto block2 = (uint16_t)((esp_netif_htonl(ip6addr.addr[0])) & 0xffff);
    auto block3 = (uint16_t)((esp_netif_htonl(ip6addr.addr[1]) >> 16) & 0xffff);
    auto block4 = (uint16_t)((esp_netif_htonl(ip6addr.addr[1])) & 0xffff);
    auto block5 = (uint16_t)((esp_netif_htonl(ip6addr.addr[2]) >> 16) & 0xffff);
    auto block6 = (uint16_t)((esp_netif_htonl(ip6addr.addr[2])) & 0xffff);
    auto block7 = (uint16_t)((esp_netif_htonl(ip6addr.addr[3]) >> 16) & 0xffff);
    auto block8 = (uint16_t)((esp_netif_htonl(ip6addr.addr[3])) & 0xffff);
    return decToHexa(block1) + ":" + 
           decToHexa(block2) + ":" + 
           decToHexa(block3) + ":" + 
           decToHexa(block4) + ":" + 
           decToHexa(block5) + ":" +  
           decToHexa(block6) + ":" +  
           decToHexa(block7) + ":" +  
           decToHexa(block8);
}
